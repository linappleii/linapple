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

auto peripheral_get_builtin_registry() -> std::vector<Peripheral_t*>& {
  static std::vector<Peripheral_t*> registry;
  return registry;
}

auto peripheral_register_builtin(Peripheral_t* p) -> void {
  if (p != nullptr) {
    peripheral_get_builtin_registry().push_back(p);
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

static constexpr size_t io_direct_count = 64;
static std::array<DirectIoHandler_t, io_direct_count> g_direct_io_handlers;
static size_t g_num_direct_handlers = 0;

static constexpr uint16_t addr_slot_io_base = 0x70;
static constexpr uint16_t addr_slot_shift = 4;
static constexpr uint16_t addr_slot_rom_shift = 8;
static constexpr uint16_t addr_slot_rom_mask = 0x07;

// --- Bridge Functions ---

static auto slot_read_c0_bridge(uint16_t pc, uint16_t addr, uint8_t write,
                                uint8_t d, uint32_t cycles_left) -> uint8_t {
  int slot = (addr & addr_slot_io_base) >> addr_slot_shift;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.readC0 != nullptr) {
      return ap.readC0(ap.instance, pc, addr, write, d, cycles_left);
    }
  }
  return io_null(pc, addr, write, d, cycles_left);
}

static auto slot_write_c0_bridge(uint16_t pc, uint16_t addr, uint8_t write,
                                 uint8_t d, uint32_t cycles_left) -> uint8_t {
  int slot = (addr & addr_slot_io_base) >> addr_slot_shift;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.writeC0 != nullptr) {
      return ap.writeC0(ap.instance, pc, addr, write, d, cycles_left);
    }
  }
  return io_null(pc, addr, write, d, cycles_left);
}

static auto slot_read_cx_bridge(uint16_t pc, uint16_t addr, uint8_t write,
                                uint8_t d, uint32_t cycles_left) -> uint8_t {
  int slot = (addr >> addr_slot_rom_shift) & addr_slot_rom_mask;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.readCx != nullptr) {
      return ap.readCx(ap.instance, pc, addr, write, d, cycles_left);
    }
  }
  return io_null(pc, addr, write, d, cycles_left);
}

static auto slot_write_cx_bridge(uint16_t pc, uint16_t addr, uint8_t write,
                                 uint8_t d, uint32_t cycles_left) -> uint8_t {
  int slot = (addr >> addr_slot_rom_shift) & addr_slot_rom_mask;
  for (auto& ap : g_active_peripherals.at(static_cast<size_t>(slot))) {
    if (ap.writeCx != nullptr) {
      return ap.writeCx(ap.instance, pc, addr, write, d, cycles_left);
    }
  }
  return io_null(pc, addr, write, d, cycles_left);
}

static auto direct_io_read_bridge(uint16_t pc, uint16_t addr, uint8_t write,
                                  uint8_t d, uint32_t cycles_left) -> uint8_t {
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    if (g_direct_io_handlers.at(i).addr == addr &&
        g_direct_io_handlers.at(i).read != nullptr) {
      return g_direct_io_handlers.at(i).read(
          g_direct_io_handlers.at(i).instance, pc, addr, write, d, cycles_left);
    }
  }
  return io_null(pc, addr, write, d, cycles_left);
}

static auto direct_io_write_bridge(uint16_t pc, uint16_t addr, uint8_t write,
                                   uint8_t d, uint32_t cycles_left) -> uint8_t {
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    if (g_direct_io_handlers.at(i).addr == addr &&
        g_direct_io_handlers.at(i).write != nullptr) {
      return g_direct_io_handlers.at(i).write(
          g_direct_io_handlers.at(i).instance, pc, addr, write, d, cycles_left);
    }
  }
  return io_null(pc, addr, write, d, cycles_left);
}

// --- Host Interface Implementation ---

static auto host_log(void* instance, PeripheralLogLevel_t level,
                     const char* fmt, ...) -> void {
  (void)instance;
  if (fmt == nullptr) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  switch (level) {
    case log_debug:
      Logger::perf(fmt, args);
      break;
    case log_info:
      Logger::info(fmt, args);
      break;
    case log_warn:
      Logger::warning(fmt, args);
      break;
    case log_error:
      Logger::error(fmt, args);
      break;
    default:
      break;
  }
  va_end(args);
}

static auto host_assert_irq(int slot, bool assert) -> void {
  if (slot < 1 || slot > 7) {
    return;
  }
  auto src = static_cast<IrqSrc_t>(is_slot1 + slot - 1);
  if (assert) {
    cpu_irq_assert(src);
  } else {
    cpu_irq_deassert(src);
  }
}

static auto host_register_io(int slot, PeripheralIOHandler readC0,
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

  register_io_handler(
      static_cast<uint32_t>(slot), readC0 ? slot_read_c0_bridge : nullptr,
      writeC0 ? slot_write_c0_bridge : nullptr,
      readCx ? slot_read_cx_bridge : nullptr,
      writeCx ? slot_write_cx_bridge : nullptr, ap.instance, ap.expansionRom);
}

static constexpr int min_slot_with_rom = 1;
static constexpr int max_slot_with_rom = 7;
static constexpr size_t cxrom_slot_size = 256;

static auto host_register_cx_rom(int slot, uint8_t* rom_ptr) -> void {
  if (slot < min_slot_with_rom || slot > max_slot_with_rom ||
      rom_ptr == nullptr)
    return;
  uint8_t* cxrom = mem_get_cx_rom_peripheral();
  if (cxrom != nullptr) {
    memcpy(cxrom + (static_cast<uint16_t>(slot) << addr_slot_rom_shift),
           rom_ptr, cxrom_slot_size);
  }
}

static auto host_register_expansion_rom(int slot, uint8_t* rom_ptr) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) return;

  ActivePeripheral_t& ap = slot_peripherals.back();
  ap.expansionRom = rom_ptr;
  register_io_handler(static_cast<uint32_t>(slot),
                      ap.readC0 ? slot_read_c0_bridge : nullptr,
                      ap.writeC0 ? slot_write_c0_bridge : nullptr,
                      ap.readCx ? slot_read_cx_bridge : nullptr,
                      ap.writeCx ? slot_write_cx_bridge : nullptr, ap.instance,
                      ap.expansionRom);
}

static auto host_register_direct_io(void* instance, uint16_t addr,
                                    PeripheralIOHandler read,
                                    PeripheralIOHandler write) -> void {
  if (g_num_direct_handlers >= io_direct_count) {
    Logger::error("Too many direct IO handlers registered!");
    return;
  }

  g_direct_io_handlers.at(g_num_direct_handlers) = {addr, read, write,
                                                    instance};
  g_num_direct_handlers++;

  register_direct_io_handler(addr, read ? direct_io_read_bridge : nullptr,
                             write ? direct_io_write_bridge : nullptr,
                             instance);
}

static auto host_get_mem_ptr(uint16_t addr) -> uint8_t* {
  return get_mem_ptr(addr);
}

static auto host_get_cycles() -> uint64_t {
  return cpu_get_cumulative_cycles();
}

static auto host_get_config(const char* section, const char* key, char* buffer,
                            size_t buffer_size) -> bool {
  std::string val;
  if (config_load_string(section, key, &val)) {
    if (buffer != nullptr && buffer_size > 0) {
      strncpy(buffer, val.c_str(), buffer_size - 1);
      buffer[buffer_size - 1] = '\0';
    }
    return true;
  }
  return false;
}

static auto host_set_config(const char* section, const char* key,
                            const char* value) -> void {
  config_save_string(section, key, value);
}

static auto host_notify_status_changed(int slot) -> void {
  (void)slot;
  extern void frame_refresh_status(int drawflags);
  frame_refresh_status(static_cast<int>(DRAW_LEDS | DRAW_BUTTON_DRIVES));
}

static auto host_notify_activity_changed(int slot, bool active) -> void {
  if (slot >= 0 && slot < static_cast<int>(NUM_SLOTS)) {
    g_peripheral_activity_state.at(static_cast<size_t>(slot)) = active;
  }
}

static auto host_request_precise_timing() -> void {
  g_state.needsprecision = static_cast<uint32_t>(cumulative_cycles);
}

static auto host_audio_push_samples(void* instance, const int16_t* buffer,
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

extern void cpu_reset();

static auto host_reset_system(void* instance) -> void {
  (void)instance;
  cpu_reset();
  peripheral_manager_reset();
}

extern void printer_frontend_send_char(uint8_t c);
extern auto printer_frontend_check_status() -> uint8_t;

static auto host_printer_put_char(void* instance, uint8_t c) -> void {
  (void)instance;
  printer_frontend_send_char(c);
}

static auto host_printer_get_status(void* instance) -> uint8_t {
  (void)instance;
  return printer_frontend_check_status();
}

extern void super_serial_frontend_send_byte(uint8_t byte);
extern auto super_serial_frontend_is_active() -> bool;
extern void super_serial_frontend_update_state(uint32_t baud, uint32_t bits,
                                               int parity, int stop);

static auto host_serial_transmit_byte(void* instance, uint8_t byte) -> void {
  (void)instance;
  super_serial_frontend_send_byte(byte);
}

static auto host_serial_update_state(void* instance, uint32_t baud,
                                     uint32_t bits, int parity, int stop)
    -> void {
  (void)instance;
  if (super_serial_frontend_is_active()) {
    super_serial_frontend_update_state(baud, bits, parity, stop);
  }
}

static const HostInterface_t g_host_interface = {host_log,
                                                 host_assert_irq,
                                                 host_register_io,
                                                 host_register_cx_rom,
                                                 host_register_expansion_rom,
                                                 host_register_direct_io,
                                                 host_get_mem_ptr,
                                                 host_get_cycles,
                                                 host_get_config,
                                                 host_set_config,
                                                 host_notify_status_changed,
                                                 host_notify_activity_changed,
                                                 host_request_precise_timing,
                                                 host_audio_push_samples,
                                                 host_reset_system,
                                                 host_printer_put_char,
                                                 host_printer_get_status,
                                                 host_serial_transmit_byte,
                                                 host_serial_update_state};

// --- Command Queue ---

struct QueuedCommand_t {
  int slot;
  uint32_t cmd_id;
  size_t data_size;
  uint8_t data[PERIPHERAL_CMD_MAX_DATA] = {};
};

static_assert(sizeof(((QueuedCommand_t*)0)->data) == PERIPHERAL_CMD_MAX_DATA,
              "QueuedCommand_t::data size must match PERIPHERAL_CMD_MAX_DATA");

static std::queue<QueuedCommand_t> g_command_queue;
static std::mutex g_command_queue_mutex;

static auto peripheral_drain_command_queue() -> void {
  std::queue<QueuedCommand_t> local;
  {
    std::lock_guard<std::mutex> lock(g_command_queue_mutex);
    std::swap(local, g_command_queue);
  }
  while (!local.empty()) {
    const QueuedCommand_t& cmd = local.front();
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

static auto clear_all_peripherals() -> void {
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    register_direct_io_handler(g_direct_io_handlers.at(i).addr, nullptr,
                               nullptr, nullptr);
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

auto peripheral_manager_init() -> void {
  {
    std::lock_guard<std::mutex> lock(g_command_queue_mutex);
    g_command_queue = {};
  }
  clear_all_peripherals();
}

auto peripheral_manager_reset() -> void {
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    for (auto& ap : g_active_peripherals.at(i)) {
      if (ap.api != nullptr && ap.api->reset != nullptr) {
        ap.api->reset(ap.instance);
      }
    }
  }
}

auto peripheral_manager_shutdown() -> void {
  clear_all_peripherals();
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

auto peripheral_manager_think(uint32_t cycles) -> void {
  peripheral_drain_command_queue();
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    for (auto& ap : g_active_peripherals.at(i)) {
      if (ap.api != nullptr && ap.api->think != nullptr) {
        ap.api->think(ap.instance, cycles);
      }
    }
  }
}

auto peripheral_manager_on_vblank(bool vblank) -> void {
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    for (auto& ap : g_active_peripherals.at(i)) {
      if (ap.api != nullptr && ap.api->on_vblank != nullptr) {
        ap.api->on_vblank(ap.instance, vblank);
      }
    }
  }
}

auto peripheral_is_any_active() -> bool {
  for (size_t i = 0; i < NUM_SLOTS; ++i) {
    if (g_peripheral_activity_state.at(i)) {
      return true;
    }
  }
  return false;
}

auto peripheral_register(Peripheral_t* api, int slot) -> int {
  if (api == nullptr || slot < 0 || slot >= static_cast<int>(NUM_SLOTS))
    return -1;
  if (api->abi_version != LINAPPLE_ABI_VERSION) return -1;

  if (!(api->compatible_slots & (1u << static_cast<uint32_t>(slot)))) {
    return -1;
  }

  if (slot != 0 &&
      !g_active_peripherals.at(static_cast<size_t>(slot)).empty()) {
    Logger::warning(
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

static auto remove_direct_io_handlers_for_instance(void* instance) -> void {
  if (instance == nullptr) return;

  size_t j = 0;
  for (size_t i = 0; i < g_num_direct_handlers; ++i) {
    if (g_direct_io_handlers.at(i).instance == instance) {
      register_direct_io_handler(g_direct_io_handlers.at(i).addr, nullptr,
                                 nullptr, nullptr);
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

auto peripheral_unregister(int slot) -> int {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS)) return -1;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  for (auto& ap : slot_peripherals) {
    remove_direct_io_handlers_for_instance(ap.instance);
    if (ap.api != nullptr && ap.api->shutdown != nullptr) {
      ap.api->shutdown(ap.instance);
    }
  }
  slot_peripherals.clear();
  register_io_handler(static_cast<uint32_t>(slot), nullptr, nullptr, nullptr,
                      nullptr, nullptr, nullptr);
  return 0;
}

auto peripheral_command(int slot, uint32_t cmd_id, const void* data,
                        size_t size) -> PeripheralStatus_t {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) ||
      size > PERIPHERAL_CMD_MAX_DATA)
    return peripheral_error;
  QueuedCommand_t cmd{};
  cmd.slot = slot;
  cmd.cmd_id = cmd_id;
  cmd.data_size = size;
  if (size > 0 && data != nullptr) memcpy(cmd.data, data, size);
  std::lock_guard<std::mutex> lock(g_command_queue_mutex);
  g_command_queue.push(cmd);
  return peripheral_ok;
}

auto peripheral_query(int slot, uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus_t {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) || out == nullptr ||
      out_size == nullptr)
    return peripheral_error;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) return peripheral_error;

  for (auto& ap : slot_peripherals) {
    if (ap.api != nullptr && ap.api->query != nullptr) {
      PeripheralStatus_t status =
          ap.api->query(ap.instance, cmd_id, out, out_size);
      if (status != peripheral_incompatible) return status;
    }
  }
  return peripheral_error;
}

auto peripheral_get_manifest(void* manifest_ptr) -> void {
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

auto peripheral_verify_manifest(const void* manifest_ptr) -> bool {
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

auto peripheral_save_state(int slot, void* buffer, size_t* size) -> void {
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

auto peripheral_load_state(int slot, const void* buffer, size_t size) -> void {
  if (slot < 0 || slot >= static_cast<int>(NUM_SLOTS) || buffer == nullptr)
    return;
  auto& slot_peripherals = g_active_peripherals.at(static_cast<size_t>(slot));
  if (slot_peripherals.empty()) return;

  auto& ap = slot_peripherals.front();
  if (ap.api != nullptr && ap.api->load_state != nullptr) {
    ap.api->load_state(ap.instance, buffer, size);
  }
}

auto peripheral_save_state_by_name(int slot, const char* name, void* buffer,
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

auto peripheral_load_state_by_name(int slot, const char* name,
                                   const void* buffer, size_t size) -> void {
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
