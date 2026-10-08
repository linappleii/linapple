// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/peripherals/keyboard/Keyboard.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"

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
// Older readers take a zero here as a repeat armed for ever.
constexpr uint32_t frame_no_repeat_key = 0xFFFFFFFF;

constexpr int8_t default_slot_internal = 0;

constexpr uint16_t addr_keyboard_data_lo = 0xC000;
constexpr uint16_t addr_keyboard_data_hi = 0xC00F;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint16_t addr_keyboard_strobe_hi = 0xC01F;

// The IOU repeats a held key after 32 to 48 scans, then every four (Sather,
// Understanding the Apple IIe, 2-17); its delay generator is clocked by F3 of
// the flash counter (3-18), so the first repeat falls on the F3 edge 32 or
// more frames after the press. F3's 16-frame period is inferred from Sather's
// 267 ms spread, its phase counted in frames since power-on.
constexpr uint64_t repeat_delay_frames = 32;
constexpr uint64_t repeat_phase_frames = 16;
constexpr uint64_t repeat_period_frames = 4;
constexpr uint32_t ntsc_frame_cycles = 17030;

// The II's REPT key runs the 555 at U3 (R3 = 220 k) at about ten presses a
// second (Apple II Reference Manual 1979, pp. 7 and 102); the II Plus's
// AY-5-3600 REPEAT oscillator, gated by ANY KEY DOWN, runs at about 15 Hz
// (Sather, Understanding the Apple II, 7-46, 7-47). Both are RC oscillators,
// so the period is wall time from the clock.
constexpr double rept_rate_apple2_hz = 10.0;
constexpr double rept_rate_apple2_plus_hz = 15.0;

// The //e keyboard is N-key rollover (Sather, Understanding the Apple IIe,
// 7-11), so no bound is the hardware's; sixteen is more than a hand.
constexpr size_t held_key_capacity = 16;

struct KeyboardHardware_t {
  uint8_t current_latch = 0;
  bool strobe = false;
  std::array<uint32_t, held_key_capacity> held{};
  size_t held_count = 0;
  bool rept_down = false;
  // The cycle of the next repeat strobe; 0 when none is armed.
  uint64_t next_strobe = 0;
};

struct KeyboardPeripheral_t {
  KeyboardHardware_t logic{};
  HostInterface_t* host = nullptr;
  int slot = 0;
  PeripheralMachine_t machine = peripheral_machine_apple2e;
};

auto any_key_down(const KeyboardPeripheral_t* kp) -> bool {
  return kp->logic.held_count > 0;
}

auto held_index(const KeyboardPeripheral_t* kp, uint32_t host_key) -> size_t {
  for (size_t i = 0; i < kp->logic.held_count; ++i) {
    if (kp->logic.held.at(i) == host_key) {
      return i;
    }
  }
  return kp->logic.held_count;
}

auto hold_key(KeyboardPeripheral_t* kp, uint32_t host_key) -> void {
  if (held_index(kp, host_key) < kp->logic.held_count) {
    return;
  }
  if (kp->logic.held_count == held_key_capacity) {
    for (size_t i = 1; i < held_key_capacity; ++i) {
      kp->logic.held.at(i - 1) = kp->logic.held.at(i);
    }
    kp->logic.held_count--;
  }
  kp->logic.held.at(kp->logic.held_count) = host_key;
  kp->logic.held_count++;
}

auto let_go_key(KeyboardPeripheral_t* kp, uint32_t host_key) -> void {
  const size_t index = held_index(kp, host_key);
  if (index == kp->logic.held_count) {
    return;
  }
  for (size_t i = index + 1; i < kp->logic.held_count; ++i) {
    kp->logic.held.at(i - 1) = kp->logic.held.at(i);
  }
  kp->logic.held_count--;
}

auto frame_cycles(const KeyboardPeripheral_t* kp) -> uint64_t {
  const uint32_t cycles = kp->host->GetFrameCycles();
  return cycles != 0 ? cycles : ntsc_frame_cycles;
}

auto rept_period(const KeyboardPeripheral_t* kp) -> uint64_t {
  const double rate = kp->machine == peripheral_machine_apple2
                          ? rept_rate_apple2_hz
                          : rept_rate_apple2_plus_hz;
  const auto period = static_cast<uint64_t>(kp->host->GetClockHz() / rate);
  return period != 0 ? period : 1;
}

auto arm(KeyboardPeripheral_t* kp, uint64_t at_cycle) -> void {
  kp->logic.next_strobe = at_cycle;
  kp->host->ScheduleEvent(kp, at_cycle);
}

auto disarm(KeyboardPeripheral_t* kp) -> void {
  kp->logic.next_strobe = 0;
  kp->host->ScheduleEvent(kp, 0);
}

// KSTRB restarts the delay generator, so a second key counts 32 frames afresh.
auto arm_auto_repeat(KeyboardPeripheral_t* kp) -> void {
  const uint64_t frame = frame_cycles(kp);
  const uint64_t press_frame = kp->host->GetCycles() / frame;
  const uint64_t phase =
      (repeat_phase_frames - press_frame % repeat_phase_frames) %
      repeat_phase_frames;
  arm(kp, (press_frame + repeat_delay_frames + phase) * frame);
}

auto arm_rept(KeyboardPeripheral_t* kp) -> void {
  arm(kp, kp->host->GetCycles() + rept_period(kp));
}

// The repeat sets KEYSTROBE alone, so the latch keeps the last code pressed
// while any matrix key is held (Apple IIe Technical Reference Manual, p. 10).
auto repeat_strobe(KeyboardPeripheral_t* kp, uint64_t now, uint64_t period)
    -> void {
  kp->logic.strobe = true;
  // A wake far past due collapses into one strobe with the phase kept.
  do {
    kp->logic.next_strobe += period;
  } while (kp->logic.next_strobe <= now);
  kp->host->ScheduleEvent(kp, kp->logic.next_strobe);
}

auto keyboard_io_read_data(void* instance, uint16_t pc, uint16_t addr,
                           uint8_t write, uint8_t val, uint32_t executed_cycles)
    -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)val;
  (void)executed_cycles;

  if (instance == nullptr) {
    return 0;
  }
  const auto* kp = static_cast<const KeyboardPeripheral_t*>(instance);

  uint8_t data = kp->logic.current_latch & key_code_mask;
  if (kp->logic.strobe) {
    data |= key_strobe_bit;
  }
  return data;
}

// On a //e any access to $C010 or a write to $C01X resets KEYSTROBE, and a
// read of $C010 returns any-key-down over the code (Sather, Understanding the
// Apple IIe, 7-4, 2-17; IIe Technical Reference pp. 12-13).
auto keyboard_io_strobe_apple2e(void* instance, uint16_t pc, uint16_t addr,
                                uint8_t write, uint8_t val,
                                uint32_t executed_cycles) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)val;
  (void)executed_cycles;

  if (instance == nullptr) {
    return 0;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  kp->logic.strobe = false;

  uint8_t data = kp->logic.current_latch & key_code_mask;
  if (any_key_down(kp)) {
    data |= key_strobe_bit;
  }
  return data;
}

// On a II any access to $C01X resets the strobe flip-flop at B10 and nothing
// drives the bus on the read (Sather, Understanding the Apple II, 7-4, 7-5,
// 5-25).
auto keyboard_io_strobe_apple2(void* instance, uint16_t pc, uint16_t addr,
                               uint8_t write, uint8_t val,
                               uint32_t executed_cycles) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)val;

  if (instance == nullptr) {
    return 0;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  kp->logic.strobe = false;
  return kp->host->ReadFloatingBus(executed_cycles);
}

// Better no card than a phantom one that cannot be reached, timed or told its
// board; the log names the missing member.
auto missing_host_member(const HostInterface_t* host) -> const char* {
  if (host->RegisterDirectIO == nullptr) {
    return "RegisterDirectIO";
  }
  if (host->ReadFloatingBus == nullptr) {
    return "ReadFloatingBus";
  }
  if (host->GetCycles == nullptr) {
    return "GetCycles";
  }
  if (host->ScheduleEvent == nullptr) {
    return "ScheduleEvent";
  }
  if (host->GetClockHz == nullptr) {
    return "GetClockHz";
  }
  if (host->GetMachine == nullptr) {
    return "GetMachine";
  }
  if (host->GetFrameCycles == nullptr) {
    return "GetFrameCycles";
  }
  return nullptr;
}

auto keyboard_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  const char* missing = missing_host_member(host);
  if (missing != nullptr) {
    if (host->Log != nullptr) {
      host->Log(nullptr, log_error,
                "Keyboard in slot %d: the host offers no %s\n", slot, missing);
    }
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
  kp->machine = host->GetMachine();

  for (uint32_t addr = addr_keyboard_data_lo; addr <= addr_keyboard_data_hi;
       ++addr) {
    host->RegisterDirectIO(kp, static_cast<uint16_t>(addr),
                           keyboard_io_read_data, nullptr);
  }
  if (kp->machine == peripheral_machine_apple2e) {
    // $C011-$C01F read as MMU and IOU flags, which the motherboard answers.
    host->RegisterDirectIO(kp, addr_keyboard_strobe, keyboard_io_strobe_apple2e,
                           keyboard_io_strobe_apple2e);
    for (uint32_t addr = addr_keyboard_strobe + 1;
         addr <= addr_keyboard_strobe_hi; ++addr) {
      host->RegisterDirectIO(kp, static_cast<uint16_t>(addr), nullptr,
                             keyboard_io_strobe_apple2e);
    }
  } else {
    for (uint32_t addr = addr_keyboard_strobe; addr <= addr_keyboard_strobe_hi;
         ++addr) {
      host->RegisterDirectIO(kp, static_cast<uint16_t>(addr),
                             keyboard_io_strobe_apple2,
                             keyboard_io_strobe_apple2);
    }
  }

  return kp_ptr.release();
}

// A hard reset is power-on: the power-up pulse clears the strobe flip-flop
// (Sather, Understanding the Apple II, 7-15) and the latch's contents are
// undocumented, so 0. The bridge never calls this for a soft reset: RESET'
// leaves the latch alone (6-17) and the Monitor clears the strobe itself.
auto keyboard_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  kp->logic.current_latch = 0;
  kp->logic.strobe = false;
  kp->logic.held_count = 0;
  kp->logic.rept_down = false;
  disarm(kp);
}

auto keyboard_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<KeyboardPeripheral_t> kp(
      static_cast<KeyboardPeripheral_t*>(instance));
}

// A wake and a command drain's think look alike, so time is GetCycles against
// the armed cycle, never the argument.
auto keyboard_abi_think(void* instance, uint32_t cycles) -> void {
  (void)cycles;
  if (instance == nullptr) {
    return;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  if (kp->logic.next_strobe == 0) {
    return;
  }
  const uint64_t now = kp->host->GetCycles();
  if (now < kp->logic.next_strobe) {
    return;
  }
  if (kp->machine == peripheral_machine_apple2e) {
    if (!any_key_down(kp)) {
      disarm(kp);
      return;
    }
    repeat_strobe(kp, now, repeat_period_frames * frame_cycles(kp));
    return;
  }
  if (!kp->logic.rept_down || !any_key_down(kp)) {
    disarm(kp);
    return;
  }
  repeat_strobe(kp, now, rept_period(kp));
}

// The encoder's KSTRB pulse loads the latch and sets the strobe (Apple II
// Reference Manual 1979, p. 102; Sather, Understanding the Apple IIe, 7-4).
auto press_key(KeyboardPeripheral_t* kp, uint32_t host_key, uint8_t code)
    -> void {
  // The II and II Plus keyboards produce upper-case ASCII only (Apple II
  // Reference Manual 1979, p. 5; Sather, Understanding the Apple II, 7-13).
  if (kp->machine != peripheral_machine_apple2e && code >= 'a' && code <= 'z') {
    code = static_cast<uint8_t>(code - 'a' + 'A');
  }
  kp->logic.current_latch = code;
  kp->logic.strobe = true;
  hold_key(kp, host_key);
  if (kp->machine == peripheral_machine_apple2e) {
    arm_auto_repeat(kp);
  } else if (kp->logic.rept_down) {
    arm_rept(kp);
  }
}

auto release_key(KeyboardPeripheral_t* kp, uint32_t host_key) -> void {
  let_go_key(kp, host_key);
  if (!any_key_down(kp)) {
    disarm(kp);
  }
}

// REPT alone on a II produces "a duplicate of the last code that was
// generated" (Apple II Reference Manual 1979, p. 7); the II Plus's oscillator
// is gated by ANY KEY DOWN, so REPT alone does nothing. A //e has no REPT key.
auto set_rept(KeyboardPeripheral_t* kp, bool down) -> void {
  kp->logic.rept_down = down;
  if (kp->machine == peripheral_machine_apple2e) {
    return;
  }
  if (!down) {
    disarm(kp);
    return;
  }
  if (any_key_down(kp)) {
    arm_rept(kp);
  } else if (kp->machine == peripheral_machine_apple2) {
    kp->logic.strobe = true;
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
        press_key(kp, ev->host_key, ev->apple_code);
      }
      return peripheral_ok;
    }
    case keyboard_cmd_release_all: {
      if (size != 0) {
        return peripheral_error;
      }
      kp->logic.held_count = 0;
      disarm(kp);
      return peripheral_ok;
    }
    case keyboard_cmd_rept: {
      if (size != sizeof(uint8_t)) {
        return peripheral_error;
      }
      set_rept(kp, *static_cast<const uint8_t*>(data) != 0);
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

  const auto* kp = static_cast<const KeyboardPeripheral_t*>(instance);
  auto* ss = static_cast<KeyboardSaveState_t*>(buffer);
  std::memset(ss, 0, sizeof(KeyboardSaveState_t));

  ss->version = KEYBOARD_STATE_VERSION;
  ss->struct_size = static_cast<uint32_t>(sizeof(KeyboardSaveState_t));
  ss->current_latch = kp->logic.current_latch;
  ss->strobe = kp->logic.strobe ? 1U : 0U;
  // The held keys and a repeat in progress are the player's hands, not the
  // machine's. An older reader still keeps caps and auto-repeat here, and caps
  // down and repeat on are what it starts with.
  ss->repeat_key = frame_no_repeat_key;
  ss->caps_lock = 1;
  ss->auto_repeat_enabled = 1;

  *size = required;
  return peripheral_ok;
}

// A longer buffer loads up to struct_size; another version or size is refused.
auto keyboard_abi_load_state(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  if (instance == nullptr || buffer == nullptr ||
      size < sizeof(KeyboardSaveState_t)) {
    return peripheral_error;
  }
  const auto* ss = static_cast<const KeyboardSaveState_t*>(buffer);
  if (ss->version != KEYBOARD_STATE_VERSION ||
      ss->struct_size != sizeof(KeyboardSaveState_t)) {
    return peripheral_error;
  }

  // Only the latch and the strobe are the machine's; the rest of the frame is
  // the writer's hands or host configuration, read past.
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  kp->logic.current_latch = ss->current_latch & key_code_mask;
  kp->logic.strobe = (ss->strobe != 0);
  kp->logic.held_count = 0;
  kp->logic.rept_down = false;
  disarm(kp);

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
    .query = keyboard_abi_query,
};

}  // namespace

// peripheral_register and ActivePeripheral_t::api still take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
extern "C" auto keyboard_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&g_keyboard_peripheral);
}

PERIPHERAL_REGISTER(g_keyboard_peripheral)
