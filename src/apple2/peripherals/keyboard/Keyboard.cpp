// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/peripherals/keyboard/Keyboard.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;
extern bool full_speed;

namespace {

static_assert(sizeof(KeyboardSaveState_t) == 552,
              "the version-1 frame is part of the plugin ABI");
static_assert(offsetof(KeyboardSaveState_t, repeat_key) == 12,
              "repeat_key is where every frame written has it");
static_assert(offsetof(KeyboardSaveState_t, current_latch) == 24,
              "current_latch is where every frame written has it");
static_assert(offsetof(KeyboardSaveState_t, strobe) == 25,
              "strobe is where every frame written has it");
static_assert(offsetof(KeyboardSaveState_t, caps_lock) == 31,
              "caps_lock is where every frame written has it");
static_assert(offsetof(KeyboardSaveState_t, auto_repeat_enabled) == 35,
              "auto_repeat_enabled is where every frame written has it");

constexpr uint8_t key_strobe_bit = 0x80;
constexpr uint8_t key_code_mask = 0x7F;

// Standard Apple II repeat circuit delays (~0.5s initial, ~0.06s repeat)
constexpr uint32_t key_repeat_initial_delay = 512000;
constexpr uint32_t key_repeat_rate = 68000;
// The frame's "no repeat armed" value, which every reader of the frame
// takes as such; a zero there would arm their repeat for ever.
constexpr uint32_t no_repeat_key = 0xFFFFFFFF;

constexpr int8_t default_slot_internal = 0;

constexpr uint16_t addr_keyboard_data_lo = 0xC000;
constexpr uint16_t addr_keyboard_data_hi = 0xC00F;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint16_t addr_keyboard_strobe_hi = 0xC01F;

struct KeyboardHardware_t {
  uint8_t current_latch = 0;
  bool strobe = false;
  uint32_t keys_down_count = 0;
  bool rept_down = false;

  uint32_t repeat_key = no_repeat_key;
  uint32_t repeat_scancode = 0;
  uint32_t repeat_delay_cycles = 0;
  bool repeating = false;
};

struct KeyboardPeripheral_t {
  KeyboardHardware_t logic{};
  HostInterface_t* host = nullptr;
  int slot = 0;
};

auto keyboard_io_read_data(void* instance, uint16_t pc, uint16_t addr,
                           uint8_t write, uint8_t val, uint32_t executed_cycles)
    -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)val;

  if (instance == nullptr) {
    return mem_read_floating_bus(executed_cycles);
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  uint8_t data = kp->logic.current_latch & key_code_mask;
  if (kp->logic.strobe) {
    data |= key_strobe_bit;
  }

  return data;
}

auto keyboard_io_strobe_action(void* instance, uint16_t pc, uint16_t addr,
                               uint8_t write, uint8_t val,
                               uint32_t executed_cycles) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)val;

  if (instance == nullptr) {
    return mem_read_floating_bus(executed_cycles);
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  // Strobe latch is cleared by hardware on any access to the $C010-$C01F range.
  kp->logic.strobe = false;

  uint8_t data = kp->logic.current_latch & key_code_mask;
  if (kp->logic.keys_down_count > 0) {
    data |= key_strobe_bit;
  }

  return data;
}

auto keyboard_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->RegisterDirectIO == nullptr) {
    return nullptr;
  }

  std::unique_ptr<KeyboardPeripheral_t> kp_ptr(new (std::nothrow)
                                                   KeyboardPeripheral_t{});
  if (!kp_ptr) {
    return nullptr;
  }
  auto* kp = kp_ptr.get();
  kp->host = host;
  kp->slot = slot;

  for (uint32_t addr = addr_keyboard_data_lo; addr <= addr_keyboard_data_hi;
       ++addr) {
    host->RegisterDirectIO(kp, static_cast<uint16_t>(addr),
                           keyboard_io_read_data, nullptr);
  }
  // $C010 (KBDSTRB): read and write both clear the strobe.
  // $C011-$C01F: reads are soft-switch status (owned by Memory.cpp); writes
  // clear the strobe. Register write-only here so reads are unaffected.
  host->RegisterDirectIO(kp, addr_keyboard_strobe, keyboard_io_strobe_action,
                         keyboard_io_strobe_action);
  for (uint32_t addr = addr_keyboard_strobe + 1;
       addr <= addr_keyboard_strobe_hi; ++addr) {
    host->RegisterDirectIO(kp, static_cast<uint16_t>(addr), nullptr,
                           keyboard_io_strobe_action);
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
  kp->logic.repeat_key = no_repeat_key;
  kp->logic.repeat_delay_cycles = 0;
  kp->logic.repeating = false;
  kp->logic.rept_down = false;
}

auto keyboard_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<KeyboardPeripheral_t> kp(
      static_cast<KeyboardPeripheral_t*>(instance));
}

auto keyboard_abi_think(void* instance, uint32_t cycles) -> void {
  if (instance == nullptr || full_speed) {
    return;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  if (kp->logic.repeat_key == no_repeat_key) {
    return;
  }

  cycles = std::min(cycles, key_repeat_initial_delay);
  kp->logic.repeat_delay_cycles += cycles;
  uint32_t delay =
      kp->logic.repeating ? key_repeat_rate : key_repeat_initial_delay;

  if (kp->logic.repeat_delay_cycles >= delay) {
    kp->logic.repeating = true;
    kp->logic.repeat_delay_cycles -= delay;
    kp->logic.strobe = true;
    kp->logic.repeat_delay_cycles %= key_repeat_rate;
  }
}

// The strobe flip-flop is set by the encoder's KSTRB pulse, which also loads
// the latch (Apple II Reference Manual 1979, p. 102; Sather, Understanding the
// Apple IIe, 7-4).
auto latch_key(KeyboardPeripheral_t* kp, uint32_t host_key, uint8_t code)
    -> void {
  kp->logic.current_latch = code;
  kp->logic.strobe = true;
  kp->logic.keys_down_count++;
  kp->logic.repeat_key = code;
  kp->logic.repeat_scancode = host_key;
  kp->logic.repeat_delay_cycles = 0;
  kp->logic.repeating = false;
}

auto release_key(KeyboardPeripheral_t* kp, uint32_t host_key) -> void {
  if (kp->logic.keys_down_count > 0) {
    kp->logic.keys_down_count--;
  }
  if (host_key == kp->logic.repeat_scancode || kp->logic.keys_down_count == 0) {
    kp->logic.repeat_key = no_repeat_key;
    kp->logic.repeating = false;
  }
}

auto keyboard_abi_command(void* instance, uint32_t cmd_id, const void* data,
                          size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  if (!peripheral_cmd_is_mine(cmd_id, PERIPHERAL_SUBSYSTEM_KEYBOARD)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }

  if (size > 0 && data == nullptr) {
    return peripheral_error;
  }

  switch (static_cast<KeyboardCmd_t>(cmd_id)) {
    case keyboard_cmd_key: {
      if (size != sizeof(KeyboardKeyEvent_t)) {
        return peripheral_error;
      }
      const auto* ev = static_cast<const KeyboardKeyEvent_t*>(data);
      if (ev->apple_code > key_code_mask) {
        return peripheral_error;
      }
      if (ev->is_down == 0U) {
        release_key(kp, ev->host_key);
      } else {
        latch_key(kp, ev->host_key, ev->apple_code);
      }
      return peripheral_ok;
    }
    case keyboard_cmd_release_all: {
      if (size != 0) {
        return peripheral_error;
      }
      kp->logic.keys_down_count = 0;
      kp->logic.repeat_key = no_repeat_key;
      kp->logic.repeating = false;
      return peripheral_ok;
    }
    case keyboard_cmd_rept: {
      if (size != sizeof(uint8_t)) {
        return peripheral_error;
      }
      kp->logic.rept_down = (*static_cast<const uint8_t*>(data) != 0);
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
  ss->repeating = kp->logic.repeating ? 1U : 0U;
  // A reader that still keeps caps lock and auto-repeat in this frame takes
  // these bytes as its state; caps down and repeat on are what it starts
  // with, so the file changes nothing for it.
  ss->caps_lock = 1;
  ss->auto_repeat_enabled = 1;

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
  kp->logic.repeating = (ss->repeating != 0);

  return peripheral_ok;
}

// The keyboard has no queries: its state is read through $C000 and $C010.
auto keyboard_abi_query(void* instance, uint32_t cmd_id, void* out,
                        size_t* out_size) -> PeripheralStatus_t {
  (void)instance;
  (void)cmd_id;
  (void)out;
  if (out_size == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

static const Peripheral_t g_keyboard_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.keyboard",
    .name = "Keyboard",
    .description = "Standard Apple II keyboard emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_INTERNAL,
    .default_slot = default_slot_internal,
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

// peripheral_register and ActivePeripheral_t::api still take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
extern "C" auto keyboard_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&g_keyboard_peripheral);
}

PERIPHERAL_REGISTER(g_keyboard_peripheral)
