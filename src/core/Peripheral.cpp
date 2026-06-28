// SPDX-License-Identifier: GPL-2.0-only
#include "Peripheral.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-avoid-magic-numbers,misc-include-cleaner,google-readability-braces-around-statements,bugprone-easily-swappable-parameters,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,modernize-use-scoped-lock):
// Central peripheral dispatch manager, slot memory map bridging, and C variadic
// host callbacks
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <queue>
#include <vector>

#include "LinAppleCore.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "core/AudioMixer.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Text.h"

LinappleAudioCallback g_frontendAudioCB = nullptr;
LinappleAudioCallback g_frontendMockAudioCB = nullptr;

auto Peripheral_GetBuiltinRegistry() -> std::vector<Peripheral_t*>& {
  static std::vector<Peripheral_t*> registry;
  return registry;
}

auto Peripheral_Register_Builtin(Peripheral_t* p) -> void {
  if (p != nullptr) {
    Peripheral_GetBuiltinRegistry().push_back(p);
  }
}

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

static std::array<std::vector<ActivePeripheral_t>, NUM_SLOTS>
    g_active_peripherals;
static std::array<bool, NUM_SLOTS> g_peripheral_activity_state;

static constexpr size_t IO_DIRECT_COUNT = 64;
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
    if (ap.readC0 != nullptr) {
      return ap.readC0(ap.instance, pc, addr, bWrite, d, nCyclesLeft);
    }
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto Slot_WriteC0_Bridge(uint16_t pc, uint16_t addr, uint8_t bWrite,
                                uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  int slot = (addr & ADDR_SLOT_IO_BASE) >> ADDR_SLOT_SHIFT;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.writeC0 != nullptr) {
      return ap.writeC0(ap.instance, pc, addr, bWrite, d, nCyclesLeft);
    }
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto Slot_ReadCx_Bridge(uint16_t pc, uint16_t addr, uint8_t bWrite,
                               uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  int slot = (addr >> ADDR_SLOT_ROM_SHIFT) & ADDR_SLOT_ROM_MASK;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.readCx != nullptr) {
      return ap.readCx(ap.instance, pc, addr, bWrite, d, nCyclesLeft);
    }
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto Slot_WriteCx_Bridge(uint16_t pc, uint16_t addr, uint8_t bWrite,
                                uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  int slot = (addr >> ADDR_SLOT_ROM_SHIFT) & ADDR_SLOT_ROM_MASK;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.writeCx != nullptr) {
      return ap.writeCx(ap.instance, pc, addr, bWrite, d, nCyclesLeft);
    }
  }
  return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
}

static auto DirectIO_Read_Bridge(uint16_t pc, uint16_t addr, uint8_t bWrite,
                                 uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    if (g_direct_io_handlers.at(i).addr == addr &&
        g_direct_io_handlers.at(i).read != nullptr) {
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
        g_direct_io_handlers.at(i).write != nullptr) {
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
  if (fmt == nullptr) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  switch (level) {
    case LOG_DEBUG:
      Logger::Perf(fmt, args);
      break;
    case LOG_INFO:
      Logger::Info(fmt, args);
      break;
    case LOG_WARN:
      Logger::Warning(fmt, args);
      break;
    case LOG_ERROR:
      Logger::Error(fmt, args);
      break;
  }
  va_end(args);
}

static auto Host_AssertIrq(int slot, bool assert) -> void {
  if (slot < 1 || slot > 7) {
    return;
  }
  auto src = static_cast<eIRQSRC>(IS_SLOT1 + slot - 1);
  if (assert) {
    CpuIrqAssert(src);
  } else {
    CpuIrqDeassert(src);
  }
}

static auto Host_RegisterIO(int slot, PeripheralIOHandler readC0,
                            PeripheralIOHandler writeC0,
                            PeripheralIOHandler readCx,
                            PeripheralIOHandler writeCx) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) return;

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
  if (slot < MIN_SLOT_WITH_ROM || slot > MAX_SLOT_WITH_ROM ||
      rom_ptr == nullptr)
    return;
  uint8_t* cxrom = MemGetCxRomPeripheral();
  if (cxrom != nullptr) {
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

static auto Host_GetCycles() -> uint64_t { return CpuGetCumulativeCycles(); }

static auto Host_GetConfig(const char* section, const char* key, char* buffer,
                           size_t buffer_size) -> bool {
  std::string val;
  if (ConfigLoadString(section, key, &val)) {
    if (buffer != nullptr && buffer_size > 0) {
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
  if (buffer == nullptr || num_samples == 0) {
    return;
  }
  bool is_mockingboard = false;
  if (instance != nullptr) {
    for (size_t i = 0; i < NUM_SLOTS; ++i) {
      for (const auto& ap : g_active_peripherals.at(i)) {
        if (ap.instance == instance) {
          if (ap.api != nullptr && ap.api->id != nullptr &&
              strcmp(ap.api->id, "linapple.mockingboard") == 0) {
            is_mockingboard = true;
          }
          break;
        }
      }
    }
  }

  if (is_mockingboard) {
    if (g_frontendMockAudioCB != nullptr) {
      g_frontendMockAudioCB(buffer, num_samples);
    } else {
      audio_mixer_upload_mockingboard_samples(
          buffer, static_cast<uint32_t>(num_samples));
    }
  } else {
    if (g_frontendAudioCB != nullptr) {
      g_frontendAudioCB(buffer, num_samples);
    } else {
      audio_mixer_upload_speaker_samples(buffer,
                                         static_cast<uint32_t>(num_samples));
    }
  }
}

extern void CpuReset();

static auto Host_ResetSystem(void* instance) -> void {
  (void)instance;
  CpuReset();
  Peripheral_Manager_Reset();
}

extern void PrinterFrontend_SendChar(uint8_t c);
extern auto PrinterFrontend_CheckStatus() -> uint8_t;

static auto Host_PrinterPutChar(void* instance, uint8_t c) -> void {
  (void)instance;
  PrinterFrontend_SendChar(c);
}

static auto Host_PrinterGetStatus(void* instance) -> uint8_t {
  (void)instance;
  return PrinterFrontend_CheckStatus();
}

extern void SuperSerialFrontend_SendByte(uint8_t byte);
extern auto SuperSerialFrontend_IsActive() -> bool;
extern void SuperSerialFrontend_UpdateState(uint32_t baud, uint32_t bits,
                                            int parity, int stop);

static auto Host_SerialTransmitByte(void* instance, uint8_t byte) -> void {
  (void)instance;
  SuperSerialFrontend_SendByte(byte);
}

static auto Host_SerialUpdateState(void* instance, uint32_t baud, uint32_t bits,
                                   int parity, int stop) -> void {
  (void)instance;
  if (SuperSerialFrontend_IsActive()) {
    SuperSerialFrontend_UpdateState(baud, bits, parity, stop);
  }
}

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
                                                 Host_AudioPushSamples,
                                                 Host_ResetSystem,
                                                 Host_PrinterPutChar,
                                                 Host_PrinterGetStatus,
                                                 Host_SerialTransmitByte,
                                                 Host_SerialUpdateState};

// --- Command Queue ---

struct QueuedCommand {
  int slot;
  uint32_t cmd_id;
  size_t data_size;
  uint8_t data[PERIPHERAL_CMD_MAX_DATA];
};

static_assert(sizeof(((QueuedCommand*)0)->data) == PERIPHERAL_CMD_MAX_DATA,
              "QueuedCommand::data size must match PERIPHERAL_CMD_MAX_DATA");

static std::queue<QueuedCommand> g_command_queue;
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
        if (ap.api != nullptr && ap.api->command != nullptr) {
          ap.api->command(ap.instance, cmd.cmd_id, cmd.data, cmd.data_size);
        }
      }
    }
    local.pop();
  }
}

static auto ClearAllPeripherals() -> void {
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    RegisterDirectIoHandler(g_direct_io_handlers.at(i).addr, nullptr, nullptr,
                            nullptr);
  }
  g_num_direct_handlers = 0;
  g_direct_io_handlers.fill({});

  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    g_peripheral_activity_state.at(i) = false;
    for (auto& ap : g_active_peripherals.at(i)) {
      if (ap.api != nullptr && ap.api->shutdown != nullptr) {
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
      if (ap.api != nullptr && ap.api->reset != nullptr) {
        ap.api->reset(ap.instance);
      }
    }
  }
}

auto Peripheral_Manager_Shutdown() -> void {
  ClearAllPeripherals();
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
      if (ap.api != nullptr && ap.api->think != nullptr) {
        ap.api->think(ap.instance, cycles);
      }
    }
  }
}

auto Peripheral_Manager_OnVBlank(bool vblank) -> void {
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    for (auto& ap : g_active_peripherals.at(i)) {
      if (ap.api != nullptr && ap.api->on_vblank != nullptr) {
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
  if (api == nullptr || slot < 0 || slot >= static_cast<int>(NUM_SLOTS))
    return -1;
  if (api->abi_version != LINAPPLE_ABI_VERSION) return -1;

  if (!(api->compatible_slots & (1u << static_cast<uint32_t>(slot)))) {
    return -1;
  }

  if (slot != 0 &&
      !g_active_peripherals.at(static_cast<size_t>(slot)).empty()) {
    Logger::Warning(
        "Slot %d already has a peripheral registered. Overwriting is not "
        "permitted.\n",
        slot);
    return -1;
  }

  ActivePeripheral_t ap{};
  ap.api = api;
  ap.slot = slot;
  g_active_peripherals.at(static_cast<size_t>(slot)).push_back(ap);

  void* instance =
      api->init(slot, const_cast<HostInterface_t*>(&g_host_interface));
  if (instance == nullptr) {
    g_active_peripherals.at(static_cast<size_t>(slot)).pop_back();
    return -1;
  }

  g_active_peripherals.at(static_cast<size_t>(slot)).back().instance = instance;

  return 0;
}

static auto RemoveDirectIoHandlersForInstance(void* instance) -> void {
  if (instance == nullptr) return;

  size_t j = 0;
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    if (g_direct_io_handlers.at(i).instance == instance) {
      RegisterDirectIoHandler(g_direct_io_handlers.at(i).addr, nullptr, nullptr,
                              nullptr);
    } else {
      if (i != j) {
        g_direct_io_handlers.at(j) = g_direct_io_handlers.at(i);
      }
      j++;
    }
  }

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
    if (ap.api != nullptr && ap.api->shutdown != nullptr) {
      ap.api->shutdown(ap.instance);
    }
  }
  slot_peripherals.clear();
  RegisterIoHandler(static_cast<uint32_t>(slot), nullptr, nullptr, nullptr,
                    nullptr, nullptr, nullptr);
  return 0;
}

auto Peripheral_Command(int slot, uint32_t cmd_id, const void* data,
                        size_t size) -> PeripheralStatus {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) ||
      size > PERIPHERAL_CMD_MAX_DATA)
    return PERIPHERAL_ERROR;
  QueuedCommand cmd{};
  cmd.slot = slot;
  cmd.cmd_id = cmd_id;
  cmd.data_size = size;
  if (size > 0 && data != nullptr) memcpy(cmd.data, data, size);
  std::lock_guard<std::mutex> lock(g_command_queue_mutex);
  g_command_queue.push(cmd);
  return PERIPHERAL_OK;
}

auto Peripheral_Query(int slot, uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) || out == nullptr ||
      out_size == nullptr)
    return PERIPHERAL_ERROR;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) return PERIPHERAL_ERROR;

  for (auto& ap : slot_peripherals) {
    if (ap.api != nullptr && ap.api->query != nullptr) {
      PeripheralStatus status =
          ap.api->query(ap.instance, cmd_id, out, out_size);
      if (status != PERIPHERAL_INCOMPATIBLE) return status;
    }
  }
  return PERIPHERAL_ERROR;
}

auto Peripheral_GetManifest(void* manifest_ptr) -> void {
  if (manifest_ptr == nullptr) return;
  auto* manifest = static_cast<SS_PERIPHERAL_MANIFEST*>(manifest_ptr);
  memset(manifest, 0, sizeof(SS_PERIPHERAL_MANIFEST));
  manifest->unit_hdr.length = sizeof(SS_PERIPHERAL_MANIFEST);
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    const auto& slot_peripherals = g_active_peripherals.at(i);
    if (!slot_peripherals.empty() && slot_peripherals.front().api != nullptr) {
      Util_SafeStrCpy(manifest->peripherals[i].name,
                      slot_peripherals.front().api->name, max_peripheral_name);
      manifest->peripherals[i].version =
          static_cast<uint32_t>(slot_peripherals.front().api->abi_version);
    }
  }
}

auto Peripheral_VerifyManifest(const void* manifest_ptr) -> bool {
  if (manifest_ptr == nullptr) return false;
  const auto* manifest =
      static_cast<const SS_PERIPHERAL_MANIFEST*>(manifest_ptr);
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    const auto& slot_peripherals = g_active_peripherals.at(i);
    const SS_PERIPHERAL_INFO& pi = manifest->peripherals[i];
    if (pi.name[0] == '\0') {
      if (!slot_peripherals.empty()) return false;
      continue;
    }
    if (slot_peripherals.empty() || slot_peripherals.front().api == nullptr ||
        strcmp(slot_peripherals.front().api->name, pi.name) != 0)
      return false;
  }
  return true;
}

auto Peripheral_SaveState(int slot, void* buffer, size_t* size) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) {
    if (size != nullptr) *size = 0;
    return;
  }
  auto& ap = slot_peripherals.front();
  if (ap.api != nullptr && ap.api->save_state != nullptr) {
    ap.api->save_state(ap.instance, buffer, size);
  } else if (size != nullptr) {
    *size = 0;
  }
}

auto Peripheral_LoadState(int slot, const void* buffer, size_t size) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) || buffer == nullptr)
    return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) return;

  auto& ap = slot_peripherals.front();
  if (ap.api != nullptr && ap.api->load_state != nullptr) {
    ap.api->load_state(ap.instance, buffer, size);
  }
}

auto Peripheral_SaveStateByName(int slot, const char* name, void* buffer,
                                size_t* size) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) || name == nullptr)
    return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  for (auto& ap : slot_peripherals) {
    if (ap.api != nullptr && strcmp(ap.api->name, name) == 0) {
      if (ap.api->save_state != nullptr) {
        ap.api->save_state(ap.instance, buffer, size);
      }
      return;
    }
  }
  if (size != nullptr) *size = 0;
}

auto Peripheral_LoadStateByName(int slot, const char* name, const void* buffer,
                                size_t size) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) || name == nullptr ||
      buffer == nullptr)
    return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  for (auto& ap : slot_peripherals) {
    if (ap.api != nullptr && strcmp(ap.api->name, name) == 0) {
      if (ap.api->load_state != nullptr) {
        ap.api->load_state(ap.instance, buffer, size);
      }
      return;
    }
  }
}

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-avoid-magic-numbers,misc-include-cleaner,google-readability-braces-around-statements,bugprone-easily-swappable-parameters,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,modernize-use-scoped-lock)
