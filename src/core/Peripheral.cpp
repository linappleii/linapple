#include "Peripheral.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <queue>
#include <vector>

#include "LinAppleCore.h"
#include "apple2/Memory.h"
#include "apple2/Riff.h"
#include "apple2/Structs.h"
#include "core/Common_Globals.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Text.h"

// Legacy audio callbacks
extern void DSUploadBuffer(int16_t* buffer, uint32_t num_samples);

// The frontend audio sink registered via Linapple_SetAudioCallback
LinappleAudioCallback g_frontendAudioCB = nullptr;

// --- Internal Types ---

struct ActivePeripheral_t {
  Peripheral_t* api;
  void* instance;
  int slot;
  PeripheralIOHandler readC0;
  PeripheralIOHandler writeC0;
  PeripheralIOHandler readCx;
  PeripheralIOHandler writeCx;
  uint8_t* expansionRom;
};

struct DirectIoHandler_t {
  uint16_t addr;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
  void* instance;
};

// Justification: Global arrays are necessary to track the current state of
// registered peripherals and their I/O handlers for the core memory map. Only
// Slot 0 is permitted to hold multiple peripherals.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::array<std::vector<ActivePeripheral_t>, NUM_SLOTS>
    g_active_peripherals;
// Justification: Tracking activity status globally is required for
// full-speed/turbo mode logic.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::array<bool, NUM_SLOTS> g_peripheral_activity_state;

static constexpr size_t IO_DIRECT_COUNT = 64;
// Justification: Global registration for direct I/O handlers (like the speaker
// at $C030). NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::array<DirectIoHandler_t, IO_DIRECT_COUNT> g_direct_io_handlers;
static size_t g_num_direct_handlers = 0;

static constexpr uint16_t ADDR_SLOT_IO_BASE = 0x70;
static constexpr uint16_t ADDR_SLOT_SHIFT = 4;
static constexpr uint16_t ADDR_SLOT_ROM_SHIFT = 8;
static constexpr uint16_t ADDR_SLOT_ROM_MASK = 0x07;

// --- Bridge Functions ---

static auto Slot_ReadC0_Bridge(uint16_t pc, uint16_t addr, uint8_t bWrite,
                               uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  int slot = (addr & ADDR_SLOT_IO_BASE) >> ADDR_SLOT_SHIFT;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.readC0) {
      return ap.readC0(ap.instance, pc, addr, bWrite, d, nCyclesLeft);
    }
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto Slot_WriteC0_Bridge(uint16_t pc, uint16_t addr, uint8_t bWrite,
                                uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  int slot = (addr & ADDR_SLOT_IO_BASE) >> ADDR_SLOT_SHIFT;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.writeC0) {
      return ap.writeC0(ap.instance, pc, addr, bWrite, d, nCyclesLeft);
    }
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto Slot_ReadCx_Bridge(uint16_t pc, uint16_t addr, uint8_t bWrite,
                               uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  int slot = (addr >> ADDR_SLOT_ROM_SHIFT) & ADDR_SLOT_ROM_MASK;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.readCx) {
      return ap.readCx(ap.instance, pc, addr, bWrite, d, nCyclesLeft);
    }
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto Slot_WriteCx_Bridge(uint16_t pc, uint16_t addr, uint8_t bWrite,
                                uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  int slot = (addr >> ADDR_SLOT_ROM_SHIFT) & ADDR_SLOT_ROM_MASK;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.writeCx) {
      return ap.writeCx(ap.instance, pc, addr, bWrite, d, nCyclesLeft);
    }
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto DirectIO_Read_Bridge(uint16_t pc, uint16_t addr, uint8_t bWrite,
                                 uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    if (g_direct_io_handlers.at(i).addr == addr &&
        g_direct_io_handlers.at(i).read) {
      return g_direct_io_handlers.at(i).read(
          g_direct_io_handlers.at(i).instance, pc, addr, bWrite, d,
          nCyclesLeft);
    }
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto DirectIO_Write_Bridge(uint16_t pc, uint16_t addr, uint8_t bWrite,
                                  uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    if (g_direct_io_handlers.at(i).addr == addr &&
        g_direct_io_handlers.at(i).write) {
      return g_direct_io_handlers.at(i).write(
          g_direct_io_handlers.at(i).instance, pc, addr, bWrite, d,
          nCyclesLeft);
    }
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

// --- Host Interface Implementation ---

static auto Host_Log(void* instance, PeripheralLogLevel level, const char* fmt,
                     ...) -> void {
  (void)instance;
  va_list args;
  // Justification: Variadic arguments are required by the stable Peripheral ABI
  // logging interface.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  va_start(args, fmt);
  switch (level) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    case LOG_DEBUG:
      Logger::Perf(fmt, args);
      break;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    case LOG_INFO:
      Logger::Info(fmt, args);
      break;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    case LOG_WARN:
      Logger::Warning(fmt, args);
      break;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    case LOG_ERROR:
      Logger::Error(fmt, args);
      break;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  va_end(args);
}

static auto Host_AssertIrq(int slot, bool assert) -> void {
  (void)slot;
  (void)assert;
}

static auto Host_RegisterIO(int slot, PeripheralIOHandler readC0,
                            PeripheralIOHandler writeC0,
                            PeripheralIOHandler readCx,
                            PeripheralIOHandler writeCx) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) return;

  // Use the most recently registered peripheral for IO registration (likely the
  // only one)
  ActivePeripheral_t& ap = slot_peripherals.back();
  ap.readC0 = readC0;
  ap.writeC0 = writeC0;
  ap.readCx = readCx;
  ap.writeCx = writeCx;

  RegisterIoHandler(
      static_cast<uint32_t>(slot), readC0 ? Slot_ReadC0_Bridge : nullptr,
      writeC0 ? Slot_WriteC0_Bridge : nullptr,
      readCx ? Slot_ReadCx_Bridge : nullptr,
      writeCx ? Slot_WriteCx_Bridge : nullptr, ap.instance, ap.expansionRom);
}

static constexpr int MIN_SLOT_WITH_ROM = 1;
static constexpr int MAX_SLOT_WITH_ROM = 7;
static constexpr size_t CXROM_SLOT_SIZE = 256;

static auto Host_RegisterCxROM(int slot, uint8_t* rom_ptr) -> void {
  if (slot < MIN_SLOT_WITH_ROM || slot > MAX_SLOT_WITH_ROM) return;
  uint8_t* cxrom = MemGetCxRomPeripheral();
  if (cxrom) {
    // Justification: Pointer arithmetic is necessary to offset into the
    // contiguous slot ROM space ($C100-$C7FF).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    memcpy(cxrom + (static_cast<uint16_t>(slot) << ADDR_SLOT_ROM_SHIFT),
           rom_ptr, CXROM_SLOT_SIZE);
  }
}

static auto Host_RegisterExpansionROM(int slot, uint8_t* rom_ptr) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) return;

  ActivePeripheral_t& ap = slot_peripherals.back();
  ap.expansionRom = rom_ptr;
  RegisterIoHandler(
      static_cast<uint32_t>(slot), ap.readC0 ? Slot_ReadC0_Bridge : nullptr,
      ap.writeC0 ? Slot_WriteC0_Bridge : nullptr,
      ap.readCx ? Slot_ReadCx_Bridge : nullptr,
      ap.writeCx ? Slot_WriteCx_Bridge : nullptr, ap.instance, ap.expansionRom);
}

static auto Host_RegisterDirectIO(void* instance, uint16_t addr,
                                  PeripheralIOHandler read,
                                  PeripheralIOHandler write) -> void {
  if (g_num_direct_handlers >= IO_DIRECT_COUNT) {
    Logger::Error("Too many direct IO handlers registered!");
    return;
  }

  g_direct_io_handlers.at(g_num_direct_handlers) = {addr, read, write,
                                                    instance};
  g_num_direct_handlers++;

  RegisterDirectIoHandler(addr, read ? DirectIO_Read_Bridge : nullptr,
                          write ? DirectIO_Write_Bridge : nullptr, instance);
}

static auto Host_GetMemPtr(uint16_t addr) -> uint8_t* {
  return GetMemPtr(addr);
}

static auto Host_GetCycles() -> uint64_t { return cumulativecycles; }

static auto Host_GetConfig(const char* section, const char* key, char* buffer,
                           size_t buffer_size) -> bool {
  std::string val;
  if (ConfigLoadString(section, key, &val)) {
    if (buffer && buffer_size > 0) {
      strncpy(buffer, val.c_str(), buffer_size - 1);
      buffer[buffer_size - 1] = '\0';
    }
    return true;
  }
  return false;
}

static auto Host_SetConfig(const char* section, const char* key,
                           const char* value) -> void {
  ConfigSaveString(section, key, value);
}

static auto Host_NotifyStatusChanged(int slot) -> void {
  (void)slot;
  extern void FrameRefreshStatus(int drawflags);
  FrameRefreshStatus(static_cast<int>(DRAW_LEDS | DRAW_BUTTON_DRIVES));
}

static auto Host_NotifyActivityChanged(int slot, bool active) -> void {
  if (slot >= 0 && slot < static_cast<int>(NUM_SLOTS)) {
    g_peripheral_activity_state.at(static_cast<size_t>(slot)) = active;
  }
}

static auto Host_RequestPreciseTiming() -> void {
  g_state.needsprecision = static_cast<uint32_t>(cumulativecycles);
}

static auto Host_AudioPushSamples(void* instance, const int16_t* buffer,
                                  size_t num_samples) -> void {
  (void)instance;
  if (g_frontendAudioCB) {
    g_frontendAudioCB(buffer, num_samples);
  } else {
    // Justification: const_cast is required to bridge the modern const buffer
    // from peripherals to the legacy DSUploadBuffer signature.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    DSUploadBuffer(const_cast<int16_t*>(buffer),
                   static_cast<uint32_t>(num_samples));
  }
}

extern void CpuReset();

static auto Host_ResetSystem(void* instance) -> void {
  (void)instance;
  CpuReset();
  Peripheral_Manager_Reset();
}

// Justification: Global immutable dispatch table for services provided to
// peripherals via the Peripheral ABI.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static const HostInterface_t g_host_interface = {Host_Log,
                                                 Host_AssertIrq,
                                                 Host_RegisterIO,
                                                 Host_RegisterCxROM,
                                                 Host_RegisterExpansionROM,
                                                 Host_RegisterDirectIO,
                                                 Host_GetMemPtr,
                                                 Host_GetCycles,
                                                 Host_GetConfig,
                                                 Host_SetConfig,
                                                 Host_NotifyStatusChanged,
                                                 Host_NotifyActivityChanged,
                                                 Host_RequestPreciseTiming,
                                                 RiffInitWriteFile,
                                                 RiffFinishWriteFile,
                                                 RiffPutSamples,
                                                 Host_AudioPushSamples,
                                                 Host_ResetSystem};

// --- Command Queue ---

struct QueuedCommand {
  int slot;
  uint32_t cmd_id;
  size_t data_size;
  // Justification: C-style array is required within the struct for fixed-size
  // memory packing in the queue.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  uint8_t data[PERIPHERAL_CMD_MAX_DATA];
};

static_assert(sizeof(((QueuedCommand*)0)->data) == PERIPHERAL_CMD_MAX_DATA,
              "QueuedCommand::data size must match PERIPHERAL_CMD_MAX_DATA");

// Justification: Global queue and mutex are required for thread-safe command
// processing between the core and peripherals.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::queue<QueuedCommand> g_command_queue;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::mutex g_command_queue_mutex;

static auto Peripheral_DrainCommandQueue() -> void {
  std::queue<QueuedCommand> local;
  {
    std::lock_guard<std::mutex> lock(g_command_queue_mutex);
    std::swap(local, g_command_queue);
  }
  while (!local.empty()) {
    const QueuedCommand& cmd = local.front();
    if (cmd.slot >= 0 && cmd.slot < static_cast<int>(NUM_SLOTS)) {
      for (auto& ap : g_active_peripherals.at(static_cast<size_t>(cmd.slot))) {
        if (ap.api && ap.api->command) {
          // Justification: cmd.data is a fixed-size internal buffer.
          // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
          ap.api->command(ap.instance, cmd.cmd_id, cmd.data, cmd.data_size);
        }
      }
    }
    local.pop();
  }
}

static auto ClearAllPeripherals() -> void {
  // Close the bridge window first — zero the dispatch table before freeing instances.
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    RegisterDirectIoHandler(g_direct_io_handlers.at(i).addr, nullptr, nullptr,
                            nullptr);
  }
  g_num_direct_handlers = 0;
  g_direct_io_handlers.fill({});

  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    g_peripheral_activity_state.at(i) = false;
    for (auto& ap : g_active_peripherals.at(i)) {
      if (ap.api && ap.api->shutdown) {
        ap.api->shutdown(ap.instance);
      }
    }
    g_active_peripherals.at(i).clear();
  }
}

// --- Public Core API ---

auto Peripheral_Manager_Init() -> void {
  {
    std::lock_guard<std::mutex> lock(g_command_queue_mutex);
    g_command_queue = {};
  }
  ClearAllPeripherals();
}

auto Peripheral_Manager_Reset() -> void {
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    for (auto& ap : g_active_peripherals.at(i)) {
      if (ap.api && ap.api->reset) {
        ap.api->reset(ap.instance);
      }
    }
  }
}

auto Peripheral_Manager_Shutdown() -> void {
  ClearAllPeripherals();
  // Justification: std::array::data() provides raw pointer access for memset.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  memset(g_peripheral_activity_state.data(), 0,
         sizeof(g_peripheral_activity_state));

  {
    std::lock_guard<std::mutex> lock(g_command_queue_mutex);
    while (!g_command_queue.empty()) {
      g_command_queue.pop();
    }
  }
  g_num_direct_handlers = 0;
}

auto Peripheral_Manager_Think(uint32_t cycles) -> void {
  Peripheral_DrainCommandQueue();
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    for (auto& ap : g_active_peripherals.at(i)) {
      if (ap.api && ap.api->think) {
        ap.api->think(ap.instance, cycles);
      }
    }
  }
}

auto Peripheral_Manager_OnVBlank(bool vblank) -> void {
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    for (auto& ap : g_active_peripherals.at(i)) {
      if (ap.api && ap.api->on_vblank) {
        ap.api->on_vblank(ap.instance, vblank);
      }
    }
  }
}

auto Peripheral_IsAnyActive() -> bool {
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    if (g_peripheral_activity_state.at(i)) {
      return true;
    }
  }
  return false;
}

auto Peripheral_Register(Peripheral_t* api, int slot) -> int {
  if (!api || slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return -1;
  if (api->abi_version != LINAPPLE_ABI_VERSION) return -1;

  if (!(api->compatible_slots & (1u << static_cast<uint32_t>(slot)))) {
    return -1;
  }

  // Only Slot 0 permits multiple peripherals.
  if (slot != 0 &&
      !g_active_peripherals.at(static_cast<size_t>(slot)).empty()) {
    Logger::Warning(
        "Slot %d already has a peripheral registered. Overwriting is not "
        "permitted.\n",
        slot);
    return -1;
  }

  // Pre-insert a placeholder so Host_RegisterIO (called from init) sees a
  // non-empty slot and can proceed with RegisterIoHandler. The instance is
  // filled in below.
  ActivePeripheral_t ap{};
  ap.api = api;
  ap.slot = slot;
  g_active_peripherals.at(static_cast<size_t>(slot)).push_back(ap);

  // Justification: The Peripheral ABI is a C interface; the HostInterface must
  // be passed as a non-const pointer to allow peripherals to store it, but we
  // provide a central const implementation.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  void* instance =
      api->init(slot, const_cast<HostInterface_t*>(&g_host_interface));
  if (!instance) {
    g_active_peripherals.at(static_cast<size_t>(slot)).pop_back();
    return -1;
  }

  g_active_peripherals.at(static_cast<size_t>(slot)).back().instance = instance;

  return 0;
}

static auto RemoveDirectIoHandlersForInstance(void* instance) -> void {
  if (!instance) return;

  size_t j = 0;
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    if (g_direct_io_handlers.at(i).instance == instance) {
      // Unregister from core memory map
      RegisterDirectIoHandler(g_direct_io_handlers.at(i).addr, nullptr, nullptr,
                              nullptr);
    } else {
      if (i != j) {
        g_direct_io_handlers.at(j) = g_direct_io_handlers.at(i);
      }
      j++;
    }
  }

  // Clear remaining slots
  for (size_t i = j; i < g_num_direct_handlers; ++i) {
    g_direct_io_handlers.at(i) = {};
  }
  g_num_direct_handlers = j;
}

auto Peripheral_Unregister(int slot) -> int {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return -1;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  for (auto& ap : slot_peripherals) {
    RemoveDirectIoHandlersForInstance(ap.instance);
    if (ap.api && ap.api->shutdown) {
      ap.api->shutdown(ap.instance);
    }
  }
  slot_peripherals.clear();
  RegisterIoHandler(static_cast<uint32_t>(slot), nullptr, nullptr, nullptr,
                    nullptr, nullptr, nullptr);
  return 0;
}

// Justification: Standard LinApple parameter order.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto Peripheral_Command(int slot, uint32_t cmd_id, const void* data,
                        size_t size) -> PeripheralStatus {
  // Reject payloads that exceed the fixed buffer capacity of QueuedCommand.data.
  // size == PERIPHERAL_CMD_MAX_DATA is valid as it fills the buffer exactly.
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) ||
      size > PERIPHERAL_CMD_MAX_DATA)
    return PERIPHERAL_ERROR;
  QueuedCommand cmd{};
  cmd.slot = slot;
  cmd.cmd_id = cmd_id;
  cmd.data_size = size;
  // Justification: cmd.data is a fixed-size internal buffer.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  if (size > 0 && data) memcpy(cmd.data, data, size);
  std::lock_guard<std::mutex> lock(g_command_queue_mutex);
  g_command_queue.push(cmd);
  return PERIPHERAL_OK;
}

// Justification: Standard LinApple parameter order.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto Peripheral_Query(int slot, uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return PERIPHERAL_ERROR;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) return PERIPHERAL_ERROR;

  // For queries, we return the result from the first peripheral that supports
  // it in the slot.
  for (auto& ap : slot_peripherals) {
    if (ap.api && ap.api->query) {
      PeripheralStatus status =
          ap.api->query(ap.instance, cmd_id, out, out_size);
      if (status != PERIPHERAL_INCOMPATIBLE) return status;
    }
  }
  return PERIPHERAL_ERROR;
}

auto Peripheral_GetManifest(void* manifest_ptr) -> void {
  if (!manifest_ptr) return;
  auto* manifest = static_cast<SS_PERIPHERAL_MANIFEST*>(manifest_ptr);
  memset(manifest, 0, sizeof(SS_PERIPHERAL_MANIFEST));
  manifest->UnitHdr.dwLength = sizeof(SS_PERIPHERAL_MANIFEST);
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    const auto& slot_peripherals = g_active_peripherals.at(i);
    if (!slot_peripherals.empty()) {
      // NOTE: The manifest format currently only supports ONE name per slot.
      // For multi-peripheral slots (like Slot 0), we report only the first one.
      // Verification remains robust for slots 1-7.
      // Justification: manifest->Peripherals is a fixed-size legacy structure.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
      Util_SafeStrCpy(manifest->Peripherals[i].szName,
                      slot_peripherals.front().api->name, MAX_PERIPHERAL_NAME);
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
      manifest->Peripherals[i].dwVersion =
          static_cast<uint32_t>(slot_peripherals.front().api->abi_version);
    }
  }
}

auto Peripheral_VerifyManifest(const void* manifest_ptr) -> bool {
  if (!manifest_ptr) return false;
  const auto* manifest =
      static_cast<const SS_PERIPHERAL_MANIFEST*>(manifest_ptr);
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    const auto& slot_peripherals = g_active_peripherals.at(i);
    // Justification: manifest->Peripherals is a fixed-size legacy structure.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    const SS_PERIPHERAL_INFO& pi = manifest->Peripherals[i];
    if (pi.szName[0] == '\0') {
      if (!slot_peripherals.empty()) return false;
      continue;
    }
    // NOTE: For multi-peripheral Slot 0, only the first occupant (Keyboard) is
    // verified.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    if (slot_peripherals.empty() ||
        strcmp(slot_peripherals.front().api->name, pi.szName) != 0)
      return false;
  }
  return true;
}

auto Peripheral_SaveState(int slot, void* buffer, size_t* size) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) {
    if (size) *size = 0;
    return;
  }
  // State saving currently only supports one peripheral per slot in the AWS
  // format. For Slot 0, this might need an extension, but for now we use the
  // first.
  auto& ap = slot_peripherals.front();
  if (ap.api && ap.api->save_state) {
    ap.api->save_state(ap.instance, buffer, size);
  } else if (size) {
    *size = 0;
  }
}

auto Peripheral_LoadState(int slot, const void* buffer, size_t size) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) return;

  auto& ap = slot_peripherals.front();
  if (ap.api && ap.api->load_state) {
    ap.api->load_state(ap.instance, buffer, size);
  }
}

void Peripheral_SaveStateByName(int slot, const char* name, void* buffer,
                                size_t* size) {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) || !name) return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  for (auto& ap : slot_peripherals) {
    if (ap.api && strcmp(ap.api->name, name) == 0) {
      if (ap.api->save_state) {
        ap.api->save_state(ap.instance, buffer, size);
      }
      return;
    }
  }
  if (size) *size = 0;
}

void Peripheral_LoadStateByName(int slot, const char* name, const void* buffer,
                                size_t size) {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) || !name) return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  for (auto& ap : slot_peripherals) {
    if (ap.api && strcmp(ap.api->name, name) == 0) {
      if (ap.api->load_state) {
        ap.api->load_state(ap.instance, buffer, size);
      }
      return;
    }
  }
}
