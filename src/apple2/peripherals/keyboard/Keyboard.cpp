// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/peripherals/keyboard/Keyboard.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "apple2/peripherals/keyboard/Keyboard_Maps.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"

#ifndef VERSIONSTRING
#define VERSIONSTRING "2.0.0"
#endif

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;
extern bool g_full_speed;

namespace {

static_assert(sizeof(KeyboardSaveState_t) == 552,
              "KeyboardSaveState_t must be exactly 552 bytes");

namespace kb {
constexpr uint8_t key_strobe_bit = 0x80;
constexpr uint8_t key_code_mask = 0x7F;

// Standard Apple II repeat circuit delays (~0.5s initial, ~0.06s repeat)
constexpr uint32_t key_repeat_initial_delay = 512000;
constexpr uint32_t key_repeat_rate = 68000;

constexpr int8_t default_slot_internal = 0;

constexpr uint16_t addr_keyboard_data_lo = 0xC000;
constexpr uint16_t addr_keyboard_data_hi = 0xC00F;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint16_t addr_keyboard_strobe_hi = 0xC01F;
constexpr uint16_t addr_open_apple = 0xC061;
constexpr uint16_t addr_closed_apple = 0xC062;
constexpr uint16_t addr_shift_key = 0xC063;

constexpr uint8_t key_up = 0x0B;
constexpr uint8_t key_down = 0x0A;
constexpr uint8_t key_left = 0x08;
constexpr uint8_t key_right = 0x15;
constexpr uint8_t key_delete = 0x7F;

constexpr uint32_t positional_threshold = 0x500;
}  // namespace kb

struct KeyboardHardware_t {
  // --- Register State ---
  uint8_t current_latch = 0;   // $C000 bits 0-6
  bool strobe = false;         // $C000 bit 7
  bool rocker_switch = false;  // Language rocker switch (US=false, Local=true)
  uint32_t keys_down_count = 0;  // Physical counter for Bit 7 of $C010
  bool caps_lock = true;
  uint8_t alternate_layout = 0;
  bool auto_repeat_enabled = true;

  // --- Modifiers ---
  bool shift_key = false;
  bool ctrl_key = false;
  bool open_apple = false;
  bool closed_apple = false;

  // --- Auto-repeat State ---
  uint32_t repeat_key = 0xFFFFFFFF;
  uint32_t repeat_scancode = 0;
  uint32_t repeat_delay_cycles = 0;
  bool repeating = false;

  // --- Custom Map Overrides ---
  bool has_custom_keys = false;
  uint8_t custom_map[KEYBOARD_MAP_SIZE]{};
  uint8_t custom_shift_map[KEYBOARD_MAP_SIZE]{};
  uint8_t custom_ctrl_map[KEYBOARD_MAP_SIZE]{};
  uint8_t custom_flags[KEYBOARD_MAP_SIZE]{};
};

struct KeyboardPeripheral_t {
  KeyboardHardware_t logic{};
  HostInterface_t* host = nullptr;
  int slot = 0;
};

auto keyboard_io_read_data(void* instance, uint16_t pc, uint16_t addr,
                           uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)val;

  namespace kp_const = kb;

  if (instance == nullptr) {
    return mem_read_floating_bus(cycles_left);
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  uint8_t data = kp->logic.current_latch & kp_const::key_code_mask;
  if (kp->logic.strobe) {
    data |= kp_const::key_strobe_bit;
  }

  return data;
}

auto keyboard_io_strobe_action(void* instance, uint16_t pc, uint16_t addr,
                               uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)val;
  (void)cycles_left;

  namespace kp_const = kb;

  if (instance == nullptr) {
    return mem_read_floating_bus(cycles_left);
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  // Strobe latch is cleared by hardware on any access to the $C010-$C01F range.
  kp->logic.strobe = false;

  uint8_t data = kp->logic.current_latch & kp_const::key_code_mask;
  if (kp->logic.keys_down_count > 0) {
    data |= kp_const::key_strobe_bit;
  }

  return data;
}

auto keyboard_io_read_apple_keys(void* instance, uint16_t pc, uint16_t addr,
                                 uint8_t write, uint8_t val,
                                 uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)write;
  (void)val;

  namespace kp_const = kb;

  if (instance == nullptr) {
    return mem_read_floating_bus(cycles_left);
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  bool pressed = false;
  switch (addr) {
    case kb::addr_open_apple:
      pressed = kp->logic.open_apple;
      break;
    case kb::addr_closed_apple:
      pressed = kp->logic.closed_apple;
      break;
    case kb::addr_shift_key:
      pressed = kp->logic.shift_key;
      break;
    default:
      break;
  }
  uint8_t bus = mem_read_floating_bus(cycles_left) & kp_const::key_code_mask;
  if (pressed) {
    bus |= kp_const::key_strobe_bit;
  }

  return bus;
}

auto keyboard_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->RegisterDirectIO == nullptr) {
    return nullptr;
  }
  namespace kp_const = kb;

  std::unique_ptr<KeyboardPeripheral_t> kp_ptr(new (std::nothrow)
                                                   KeyboardPeripheral_t{});
  if (!kp_ptr) {
    return nullptr;
  }
  auto* kp = kp_ptr.get();
  kp->host = host;
  kp->slot = slot;
  kp->logic.caps_lock = true;
  kp->logic.auto_repeat_enabled = true;

  if (host != nullptr && host->RegisterDirectIO != nullptr) {
    for (uint32_t addr = kp_const::addr_keyboard_data_lo;
         addr <= kp_const::addr_keyboard_data_hi; ++addr) {
      host->RegisterDirectIO(kp, static_cast<uint16_t>(addr),
                             keyboard_io_read_data, nullptr);
    }
    // $C010 (KBDSTRB): read and write both clear the strobe.
    // $C011-$C01F: reads are soft-switch status (owned by Memory.cpp); writes
    // clear the strobe. Register write-only here so reads are unaffected.
    host->RegisterDirectIO(kp, kp_const::addr_keyboard_strobe,
                           keyboard_io_strobe_action,
                           keyboard_io_strobe_action);
    for (uint32_t addr = kp_const::addr_keyboard_strobe + 1;
         addr <= kp_const::addr_keyboard_strobe_hi; ++addr) {
      host->RegisterDirectIO(kp, static_cast<uint16_t>(addr), nullptr,
                             keyboard_io_strobe_action);
    }
    host->RegisterDirectIO(kp, kp_const::addr_open_apple,
                           keyboard_io_read_apple_keys, nullptr);
    host->RegisterDirectIO(kp, kp_const::addr_closed_apple,
                           keyboard_io_read_apple_keys, nullptr);
    host->RegisterDirectIO(kp, kp_const::addr_shift_key,
                           keyboard_io_read_apple_keys, nullptr);
  }

  return kp_ptr.release();
}

auto keyboard_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  kp->logic.current_latch = 0;
  kp->logic.strobe = false;
  kp->logic.keys_down_count = 0;
  kp->logic.repeat_key = 0xFFFFFFFF;
  kp->logic.repeat_delay_cycles = 0;
  kp->logic.repeating = false;

  kp->logic.shift_key = false;
  kp->logic.ctrl_key = false;
  kp->logic.open_apple = false;
  kp->logic.closed_apple = false;
}

auto keyboard_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  delete static_cast<KeyboardPeripheral_t*>(instance);
}

auto keyboard_abi_think(void* instance, uint32_t cycles) -> void {
  if (instance == nullptr || g_full_speed) {
    return;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  if (!kp->logic.auto_repeat_enabled) {
    return;
  }

  if (kp->logic.repeat_key == 0xFFFFFFFF) {
    return;
  }

  namespace kp_const = kb;

  cycles = std::min(cycles, kp_const::key_repeat_initial_delay);
  kp->logic.repeat_delay_cycles += cycles;
  uint32_t delay = kp->logic.repeating ? kp_const::key_repeat_rate
                                       : kp_const::key_repeat_initial_delay;

  if (kp->logic.repeat_delay_cycles >= delay) {
    kp->logic.repeating = true;
    kp->logic.repeat_delay_cycles -= delay;
    kp->logic.strobe = true;
    kp->logic.repeat_delay_cycles %= kp_const::key_repeat_rate;
  }
}

auto keyboard_map_symbolic(uint32_t key) -> uint32_t {
  namespace kp_const = kb;

  // Offset extended keys to fit in a small lookup table
  if (key < 0x100 || key >= 0x110) {
    return 0xFFFFFFFF;
  }

  static constexpr uint8_t symbolic_map[] = {kp_const::key_up,
                                             kp_const::key_down,
                                             kp_const::key_left,
                                             kp_const::key_right,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             kp_const::key_delete};

  const size_t idx = key - 0x100;
  if (idx < (sizeof(symbolic_map) / sizeof(symbolic_map[0]))) {
    const uint32_t mapped = symbolic_map[idx];
    return (mapped != 0) ? mapped : 0xFFFFFFFF;
  }

  return 0xFFFFFFFF;
}

auto keyboard_map_positional(KeyboardPeripheral_t* kp, uint32_t key, bool shift,
                             bool ctrl) -> uint32_t {
  namespace kp_const = kb;

  const int idx = static_cast<int>(key - kp_const::positional_threshold);
  if (idx < 0 || idx >= KEYBOARD_MAP_SIZE) {
    return 0xFFFFFFFF;
  }

  const Apple2KeyboardMap_t* layout = &map_us;
  bool is_alternate = false;

  if (kp->logic.rocker_switch) {
    static const Apple2KeyboardMap_t* const layout_table[] = {
        &map_us, &map_uk, &map_fr, &map_de, &map_es,       &map_it,
        &map_se, &map_dk, &map_ch, &map_ca, &map_jp_roman, &map_jp_kana};

    const uint8_t alt = kp->logic.alternate_layout;
    if (alt > 0 && alt < (sizeof(layout_table) / sizeof(layout_table[0]))) {
      is_alternate = true;
      layout = layout_table[alt];
    }
  }

  uint32_t base = layout->map[idx];
  uint32_t shift_val = layout->shift_map[idx];
  uint32_t ctrl_val = layout->ctrl_map[idx];

  if (is_alternate && base == 0) {
    base = map_us.map[idx];
    shift_val = map_us.shift_map[idx];
    ctrl_val = map_us.ctrl_map[idx];
  }

  if (base == 0) {
    return 0xFFFFFFFF;
  }

  if (kp->logic.has_custom_keys && (kp->logic.custom_flags[idx] & 1) != 0) {
    base = kp->logic.custom_map[idx];
    shift_val = kp->logic.custom_shift_map[idx];
    ctrl_val = kp->logic.custom_ctrl_map[idx];
  }

  if (shift) {
    if (shift_val != 0) {
      base = shift_val;
    } else if (base >= 'a' && base <= 'z') {
      base = base - 'a' + 'A';
    }
  } else if (kp->logic.caps_lock && base >= 'a' && base <= 'z') {
    base = base - 'a' + 'A';
  }

  if (ctrl) {
    if (ctrl_val != 0) {
      return ctrl_val;
    }
    return base & 0x1F;
  }

  return base;
}

auto keyboard_apply_symbolic_shift(uint32_t key, bool shift, bool ctrl,
                                   bool caps_lock) -> uint32_t {
  if (shift) {
    switch (key) {
      case '1':
        return '!';
      case '2':
        return '@';
      case '3':
        return '#';
      case '4':
        return '$';
      case '5':
        return '%';
      case '6':
        return '^';
      case '7':
        return '&';
      case '8':
        return '*';
      case '9':
        return '(';
      case '0':
        return ')';
      case '-':
        return '_';
      case '=':
        return '+';
      case '[':
        return '{';
      case ']':
        return '}';
      case '\\':
        return '|';
      case ';':
        return ':';
      case '\'':
        return '"';
      case '`':
        return '~';
      case ',':
        return '<';
      case '.':
        return '>';
      case '/':
        return '?';
      default:
        if (key >= 'a' && key <= 'z') {
          return key - 'a' + 'A';
        }
        break;
    }
  } else if (caps_lock && key >= 'a' && key <= 'z') {
    return key - 'a' + 'A';
  }

  if (ctrl) {
    if (key >= 'a' && key <= 'z') {
      return (key - 'a' + 1);
    }
    if (key >= 'A' && key <= 'Z') {
      return (key - 'A' + 1);
    }
    return key & 0x1F;
  }

  return key;
}

auto keyboard_abi_command(void* instance, uint32_t cmd_id, const void* data,
                          size_t size) -> PeripheralStatus_t {
  if (instance == nullptr || (size > 0 && data == nullptr)) {
    return peripheral_error;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  namespace kp_const = kb;

  switch (static_cast<KeyboardCmd_t>(cmd_id)) {
    case keyboard_cmd_event: {
      if (size < sizeof(KeyboardEvent_t)) {
        return peripheral_error;
      }
      const auto* ev = static_cast<const KeyboardEvent_t*>(data);

      if (ev->is_down == 0U) {
        if (kp->logic.keys_down_count > 0) {
          kp->logic.keys_down_count--;
        }
        if (ev->key == kp->logic.repeat_scancode ||
            kp->logic.keys_down_count == 0) {
          kp->logic.repeat_key = 0xFFFFFFFF;
          kp->logic.repeating = false;
        }
        if (kp->logic.has_custom_keys &&
            ev->key >= kp_const::positional_threshold) {
          const int idx =
              static_cast<int>(ev->key - kp_const::positional_threshold);
          if (idx >= 0 && idx < KEYBOARD_MAP_SIZE) {
            if ((kp->logic.custom_flags[idx] & 2) != 0) {
              kp->logic.open_apple = false;
            }
            if ((kp->logic.custom_flags[idx] & 4) != 0) {
              kp->logic.closed_apple = false;
            }
          }
        }
        return peripheral_ok;
      }

      if (kp->logic.has_custom_keys &&
          ev->key >= kp_const::positional_threshold) {
        const int idx =
            static_cast<int>(ev->key - kp_const::positional_threshold);
        if (idx >= 0 && idx < KEYBOARD_MAP_SIZE) {
          if ((kp->logic.custom_flags[idx] & 2) != 0) {
            kp->logic.open_apple = true;
            return peripheral_ok;
          }
          if ((kp->logic.custom_flags[idx] & 4) != 0) {
            kp->logic.closed_apple = true;
            return peripheral_ok;
          }
        }
      }

      uint32_t key = ev->key;
      if (key >= kp_const::positional_threshold) {
        key = keyboard_map_positional(kp, key, ev->mod_shift != 0U,
                                      ev->mod_ctrl != 0U);
      } else if (key >= 0x100) {
        key = keyboard_map_symbolic(key);
      } else if (key != 0) {
        key = keyboard_apply_symbolic_shift(
            key, ev->mod_shift != 0U, ev->mod_ctrl != 0U, kp->logic.caps_lock);
      }

      if (key > kp_const::key_code_mask) {
        return peripheral_ok;
      }

      kp->logic.current_latch = static_cast<uint8_t>(key);
      kp->logic.strobe = true;
      kp->logic.keys_down_count++;
      kp->logic.repeat_key = static_cast<uint8_t>(key);
      kp->logic.repeat_scancode = ev->key;
      kp->logic.repeat_delay_cycles = 0;
      kp->logic.repeating = false;
      return peripheral_ok;
    }
    case keyboard_cmd_set_caps: {
      if (size < sizeof(uint8_t)) {
        return peripheral_error;
      }
      kp->logic.caps_lock = (*static_cast<const uint8_t*>(data) != 0);
      return peripheral_ok;
    }
    case keyboard_cmd_set_rocker: {
      if (size < sizeof(uint8_t)) {
        return peripheral_error;
      }
      kp->logic.rocker_switch = (*static_cast<const uint8_t*>(data) != 0);
      return peripheral_ok;
    }
    case keyboard_cmd_set_mods: {
      if (size < sizeof(KeyboardModifiers_t)) {
        return peripheral_error;
      }
      const auto* mods = static_cast<const KeyboardModifiers_t*>(data);
      kp->logic.shift_key = (mods->shift != 0);
      kp->logic.ctrl_key = (mods->ctrl != 0);

      // Map host modifiers to Apple II hardware keys.
      // Standard convention: GUI maps to Open Apple, Alt maps to BOTH Open and
      // Closed Apple.
      kp->logic.open_apple = (mods->gui != 0) || (mods->alt != 0);
      kp->logic.closed_apple = (mods->alt != 0);
      return peripheral_ok;
    }
    case keyboard_cmd_set_layout: {
      if (size < sizeof(uint8_t)) {
        return peripheral_error;
      }
      kp->logic.alternate_layout = *static_cast<const uint8_t*>(data);
      return peripheral_ok;
    }
    case keyboard_cmd_set_custom_key: {
      if (size < sizeof(KeyboardCustomKeyPayload_t)) {
        return peripheral_error;
      }
      const auto* payload =
          static_cast<const KeyboardCustomKeyPayload_t*>(data);
      if (payload->scancode >= KEYBOARD_MAP_SIZE) {
        return peripheral_error;
      }
      kp->logic.custom_map[payload->scancode] = payload->normal_val;
      kp->logic.custom_shift_map[payload->scancode] = payload->shift_val;
      kp->logic.custom_ctrl_map[payload->scancode] = payload->ctrl_val;
      kp->logic.custom_flags[payload->scancode] = payload->flags;
      kp->logic.has_custom_keys = true;
      return peripheral_ok;
    }
    case keyboard_cmd_clear_custom_keys: {
      kp->logic.has_custom_keys = false;
      std::memset(kp->logic.custom_map, 0, sizeof(kp->logic.custom_map));
      std::memset(kp->logic.custom_shift_map, 0,
                  sizeof(kp->logic.custom_shift_map));
      std::memset(kp->logic.custom_ctrl_map, 0,
                  sizeof(kp->logic.custom_ctrl_map));
      std::memset(kp->logic.custom_flags, 0, sizeof(kp->logic.custom_flags));
      return peripheral_ok;
    }
    case keyboard_cmd_set_auto_repeat: {
      if (size < sizeof(uint8_t)) {
        return peripheral_error;
      }
      kp->logic.auto_repeat_enabled = (*static_cast<const uint8_t*>(data) != 0);
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

auto keyboard_abi_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  if (size == nullptr) {
    return peripheral_error;
  }

  const size_t required = sizeof(KeyboardSaveState_t);

  if (buffer == nullptr) {
    *size = required;
    return peripheral_ok;
  }

  if (instance == nullptr || *size < required) {
    *size = required;
    return peripheral_error;
  }

  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  auto* ss = static_cast<KeyboardSaveState_t*>(buffer);
  std::memset(ss, 0, sizeof(KeyboardSaveState_t));

  ss->version = KEYBOARD_STATE_VERSION;
  ss->struct_size = static_cast<uint32_t>(sizeof(KeyboardSaveState_t));
  ss->keys_down_count = kp->logic.keys_down_count;
  ss->repeat_key = kp->logic.repeat_key;
  ss->repeat_scancode = kp->logic.repeat_scancode;
  ss->repeat_delay_cycles = kp->logic.repeat_delay_cycles;
  ss->current_latch = kp->logic.current_latch;
  ss->strobe = kp->logic.strobe ? 1U : 0U;
  ss->rocker_switch = kp->logic.rocker_switch ? 1U : 0U;
  ss->shift_key = kp->logic.shift_key ? 1U : 0U;
  ss->ctrl_key = kp->logic.ctrl_key ? 1U : 0U;
  ss->open_apple = kp->logic.open_apple ? 1U : 0U;
  ss->closed_apple = kp->logic.closed_apple ? 1U : 0U;
  ss->caps_lock = kp->logic.caps_lock ? 1U : 0U;
  ss->alternate_layout = kp->logic.alternate_layout;
  ss->repeating = kp->logic.repeating ? 1U : 0U;
  ss->has_custom_keys = kp->logic.has_custom_keys ? 1U : 0U;
  ss->auto_repeat_enabled = kp->logic.auto_repeat_enabled ? 1U : 0U;

  std::memcpy(ss->custom_map, kp->logic.custom_map, KEYBOARD_MAP_SIZE);
  std::memcpy(ss->custom_shift_map, kp->logic.custom_shift_map,
              KEYBOARD_MAP_SIZE);
  std::memcpy(ss->custom_ctrl_map, kp->logic.custom_ctrl_map,
              KEYBOARD_MAP_SIZE);
  std::memcpy(ss->custom_flags, kp->logic.custom_flags, KEYBOARD_MAP_SIZE);

  *size = required;
  return peripheral_ok;
}

auto keyboard_abi_load_state(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  if (instance == nullptr || buffer == nullptr ||
      size != sizeof(KeyboardSaveState_t)) {
    return peripheral_error;
  }
  const auto* ss = static_cast<const KeyboardSaveState_t*>(buffer);
  if (ss->version != KEYBOARD_STATE_VERSION ||
      ss->struct_size != sizeof(KeyboardSaveState_t)) {
    return peripheral_error;
  }

  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  kp->logic.keys_down_count = ss->keys_down_count;
  kp->logic.repeat_key = ss->repeat_key;
  kp->logic.repeat_scancode = ss->repeat_scancode;
  kp->logic.repeat_delay_cycles = ss->repeat_delay_cycles;
  kp->logic.current_latch = ss->current_latch;
  kp->logic.strobe = (ss->strobe != 0);
  kp->logic.rocker_switch = (ss->rocker_switch != 0);
  kp->logic.shift_key = (ss->shift_key != 0);
  kp->logic.ctrl_key = (ss->ctrl_key != 0);
  kp->logic.open_apple = (ss->open_apple != 0);
  kp->logic.closed_apple = (ss->closed_apple != 0);
  kp->logic.caps_lock = (ss->caps_lock != 0);
  kp->logic.alternate_layout = ss->alternate_layout;
  kp->logic.repeating = (ss->repeating != 0);
  kp->logic.has_custom_keys = (ss->has_custom_keys != 0);
  kp->logic.auto_repeat_enabled = (ss->auto_repeat_enabled != 0);

  std::memcpy(kp->logic.custom_map, ss->custom_map, KEYBOARD_MAP_SIZE);
  std::memcpy(kp->logic.custom_shift_map, ss->custom_shift_map,
              KEYBOARD_MAP_SIZE);
  std::memcpy(kp->logic.custom_ctrl_map, ss->custom_ctrl_map,
              KEYBOARD_MAP_SIZE);
  std::memcpy(kp->logic.custom_flags, ss->custom_flags, KEYBOARD_MAP_SIZE);

  return peripheral_ok;
}

auto keyboard_abi_query(void* instance, uint32_t cmd_id, void* out,
                        size_t* out_size) -> PeripheralStatus_t {
  if (instance == nullptr || out_size == nullptr) {
    return peripheral_error;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  switch (static_cast<KeyboardQuery_t>(cmd_id)) {
    case keyboard_query_mods: {
      if (out == nullptr) {
        *out_size = sizeof(KeyboardModifiers_t);
        return peripheral_ok;
      }
      if (*out_size < sizeof(KeyboardModifiers_t)) {
        return peripheral_error;
      }
      auto* mods = static_cast<KeyboardModifiers_t*>(out);
      mods->shift = kp->logic.shift_key ? 1U : 0U;
      mods->ctrl = kp->logic.ctrl_key ? 1U : 0U;
      mods->alt = kp->logic.closed_apple ? 1U : 0U;
      mods->gui = (kp->logic.open_apple && !kp->logic.closed_apple) ? 1U : 0U;
      mods->caps = kp->logic.caps_lock ? 1U : 0U;
      *out_size = sizeof(KeyboardModifiers_t);
      return peripheral_ok;
    }
    case keyboard_query_rocker: {
      if (out == nullptr) {
        *out_size = sizeof(uint8_t);
        return peripheral_ok;
      }
      if (*out_size < sizeof(uint8_t)) {
        return peripheral_error;
      }
      *static_cast<uint8_t*>(out) = kp->logic.rocker_switch ? 1U : 0U;
      *out_size = sizeof(uint8_t);
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

static Peripheral_t g_keyboard_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.keyboard",
    .name = "Keyboard",
    .description = "Standard Apple II keyboard emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_INTERNAL,
    .default_slot = kb::default_slot_internal,
    .init = keyboard_abi_init,
    .reset = keyboard_abi_reset,
    .shutdown = keyboard_abi_shutdown,
    .think = keyboard_abi_think,
    .on_vblank = nullptr,
    .save_state = keyboard_abi_save_state,
    .load_state = keyboard_abi_load_state,
    .command = keyboard_abi_command,
    .query = keyboard_abi_query};

}  // namespace

extern "C" auto keyboard_get_descriptor() -> Peripheral_t* {
  return &g_keyboard_peripheral;
}

PERIPHERAL_REGISTER(g_keyboard_peripheral)
