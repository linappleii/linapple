// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/joystick/Joystick.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"

namespace {

// The 74LS251 behind $C060-$C06F puts one input on D7 alone, selected by
// A0-A2; A3 is not decoded, so $C068-$C06F mirror $C060-$C067, and bits 0-6
// are the undriven bus. Inputs 0-3 are the motherboard's; the card answers
// inputs 4-7, its four timers, and any access to $C070-$C07F triggers all
// four. $C07F stays with the motherboard: on the //e it reads RDDHIRES (Apple
// II Reference Manual 1979 pp. 78-79, 99; Sather 7-8; IIe Tech Ref pp. 29,
// 41, 187).
constexpr uint16_t addr_mux_first = 0xC060;
constexpr uint16_t addr_mux_last = 0xC06F;
constexpr uint16_t addr_trigger_first = 0xC070;
constexpr uint16_t addr_trigger_last = 0xC07E;
constexpr uint16_t mux_select_mask = 0x07;
constexpr size_t mux_paddle0 = 4;
constexpr uint8_t bus_data_mask = 0x7F;
constexpr uint8_t input_bit = 0x80;

constexpr size_t paddle_count = 4;
constexpr uint8_t joystick_count = 2;
constexpr uint8_t axis_count = 2;

// Where a centred stick rests.
constexpr uint8_t centre_position = 127;

// PREAD ($FB1E) samples the timer 10 cycles after its strobe and every 11
// after that (Sather 7-24), so a pulse of 11 x position + 10 cycles makes it
// return exactly position. A count model: 255 is a 2,815-cycle pulse, not the
// 3,370 of a 150 kOhm pot at full travel (Sather 7-11), so a program polling
// faster than PREAD sees the top of the travel compressed.
constexpr uint64_t pulse_cycles_per_count = 11;
constexpr uint64_t pulse_lead_in_cycles = 10;

struct GamePort_t {
  std::array<uint64_t, paddle_count> trigger_cycle{};
  std::array<uint8_t, paddle_count> position{
      {centre_position, centre_position, centre_position, centre_position},
  };
  // The pot as it stood at the accepted strobe, moved with the pot only while
  // the output is high.
  std::array<uint8_t, paddle_count> pulse_position{};
  HostInterface_t* host = nullptr;
  int slot = 0;
};

auto pulse_cycles(uint8_t position) -> uint64_t {
  return (static_cast<uint64_t>(position) * pulse_cycles_per_count) +
         pulse_lead_in_cycles;
}

// Trigger 0 is never triggered (the NE558 output idles low, datasheet note 3);
// a trigger ahead of the counter was wound past and has long run out. The
// pulse is measured against its latched position: once the output has fallen
// the timer is idle whatever the pot does (NE558 datasheet; Sather 7-11).
auto timer_expired(const GamePort_t* port, size_t paddle, uint64_t now)
    -> bool {
  const uint64_t trigger = port->trigger_cycle.at(paddle);
  if (trigger == 0 || trigger > now) {
    return true;
  }
  return now - trigger >= pulse_cycles(port->pulse_position.at(paddle));
}

auto joystick_io_read_paddle(void* instance, uint16_t program_counter,
                             uint16_t memory_address, uint8_t is_write,
                             uint8_t data_value, uint32_t executed_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)is_write;
  (void)data_value;
  if (instance == nullptr) {
    return 0;
  }
  auto* port = static_cast<GamePort_t*>(instance);

  uint8_t result = port->host->ReadFloatingBus(executed_cycles) & bus_data_mask;
  const size_t paddle = (memory_address & mux_select_mask) - mux_paddle0;
  if (paddle >= paddle_count) {
    return result;
  }
  if (!timer_expired(port, paddle, port->host->GetCycles())) {
    result |= input_bit;
  }
  return result;
}

// A monostable: "the timers are not retriggered by C07X if they have not yet
// reset" (Sather 7-24), so only an expired channel starts a pulse. The trigger
// is recorded as at least cycle 1 so a strobe in cycle 0 is told from never.
auto joystick_strobe(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* port = static_cast<GamePort_t*>(instance);
  const uint64_t now = port->host->GetCycles();
  for (size_t paddle = 0; paddle < paddle_count; ++paddle) {
    if (timer_expired(port, paddle, now)) {
      port->trigger_cycle.at(paddle) = std::max<uint64_t>(now, 1);
      port->pulse_position.at(paddle) = port->position.at(paddle);
    }
  }
}

// Better no card than a phantom one: without these members the port cannot be
// reached, timed or read, and the log names the missing one.
auto missing_host_member(const HostInterface_t* host) -> const char* {
  if (host->RegisterDirectIO == nullptr) {
    return "RegisterDirectIO";
  }
  if (host->RegisterDirectIOStrobe == nullptr) {
    return "RegisterDirectIOStrobe";
  }
  if (host->GetCycles == nullptr) {
    return "GetCycles";
  }
  if (host->ReadFloatingBus == nullptr) {
    return "ReadFloatingBus";
  }
  return nullptr;
}

auto joystick_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  const char* missing = missing_host_member(host);
  if (missing != nullptr) {
    if (host->Log != nullptr) {
      host->Log(nullptr, log_error,
                "Game port in slot %d: the host offers no %s\n", slot, missing);
    }
    return nullptr;
  }

  auto port = std::unique_ptr<GamePort_t>(new (std::nothrow) GamePort_t());
  if (!port) {
    return nullptr;
  }
  port->host = host;
  port->slot = slot;

  for (uint16_t addr = addr_mux_first; addr <= addr_mux_last; ++addr) {
    if ((addr & mux_select_mask) < mux_paddle0) {
      continue;
    }
    host->RegisterDirectIO(port.get(), addr, joystick_io_read_paddle, nullptr);
  }
  for (uint16_t addr = addr_trigger_first; addr <= addr_trigger_last; ++addr) {
    host->RegisterDirectIOStrobe(port.get(), addr, joystick_strobe);
  }

  return port.release();
}

auto joystick_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<GamePort_t> port(static_cast<GamePort_t*>(instance));
}

auto joystick_abi_command(void* instance, uint32_t command_id,
                          const void* payload, size_t payload_size)
    -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* port = static_cast<GamePort_t*>(instance);
  if (!peripheral_cmd_is_mine(command_id, PERIPHERAL_SUBSYSTEM_JOYSTICK)) {
    return peripheral_incompatible;
  }

  if (command_id != JOYSTICK_CMD_SET_AXIS) {
    return peripheral_incompatible;
  }

  if (payload == nullptr || payload_size != sizeof(JoystickAxisPayload_t)) {
    return peripheral_error;
  }
  const auto* axis = static_cast<const JoystickAxisPayload_t*>(payload);
  if (axis->joystick >= joystick_count || axis->axis >= axis_count) {
    return peripheral_error;
  }
  const size_t paddle =
      (static_cast<size_t>(axis->joystick) * axis_count) + axis->axis;
  // The capacitor charges through the pot it has now: a move while the
  // output is high moves the fall; a move after it meets an idle timer.
  const bool running = !timer_expired(port, paddle, port->host->GetCycles());
  port->position.at(paddle) = axis->value;
  if (running) {
    port->pulse_position.at(paddle) = axis->value;
  }
  return peripheral_ok;
}

// The port has no queries: its state is read through the switches and timers.
// NOLINTNEXTLINE(readability-non-const-parameter) - signature defined by PeripheralQueryFn ABI
auto joystick_abi_query(void* instance, uint32_t query_id, void* output,
                        size_t* output_size) -> PeripheralStatus_t {
  (void)instance;
  (void)query_id;
  (void)output;
  if (output_size == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

static_assert(sizeof(JoystickSaveState_t) == 56,
              "the game port's state frame is part of the plugin ABI");
static_assert(offsetof(JoystickSaveState_t, version) == 0,
              "the frame header is version then size");
static_assert(offsetof(JoystickSaveState_t, struct_size) == 4,
              "the frame header is version then size");
static_assert(offsetof(JoystickSaveState_t, trigger_cycle) == 8,
              "the four triggers sit where every frame written has them");
static_assert(sizeof(JoystickSaveState_t::trigger_cycle) == 32,
              "one 64-bit trigger per timer");
static_assert(offsetof(JoystickSaveState_t, x_pos) == 40,
              "the fields after the triggers keep their place");
static_assert(offsetof(JoystickSaveState_t, y_pos) == 42,
              "the fields after the triggers keep their place");
static_assert(offsetof(JoystickSaveState_t, buttons) == 44,
              "the fields after the triggers keep their place");
static_assert(offsetof(JoystickSaveState_t, reserved0) == 47,
              "the fields after the triggers keep their place");
static_assert(offsetof(JoystickSaveState_t, trim_x) == 48,
              "the fields after the triggers keep their place");
static_assert(offsetof(JoystickSaveState_t, trim_y) == 50,
              "the fields after the triggers keep their place");
static_assert(offsetof(JoystickSaveState_t, reserved1) == 52,
              "the fields after the triggers keep their place");

// Positions, trim and switch levels are the host's or the motherboard's and go
// out as zeros.
auto joystick_abi_save_state(void* instance, void* state_buffer,
                             size_t* buffer_size) -> PeripheralStatus_t {
  if (buffer_size == nullptr) {
    return peripheral_error;
  }
  constexpr size_t required_size = sizeof(JoystickSaveState_t);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  const auto* port = static_cast<const GamePort_t*>(instance);
  JoystickSaveState_t state{};
  state.version = JOYSTICK_STATE_VERSION;
  state.struct_size = static_cast<uint32_t>(required_size);
  for (size_t paddle = 0; paddle < paddle_count; ++paddle) {
    state.trigger_cycle[paddle] = port->trigger_cycle.at(paddle);
  }
  std::memcpy(state_buffer, &state, required_size);

  *buffer_size = required_size;
  return peripheral_ok;
}

// Any trigger loads; one ahead of the counter reads expired. The frame carries
// no positions, so a loaded pulse is measured against the pot as the host
// holds it now (it re-sends within a slice; a pulse is under 3 ms).
auto joystick_abi_load_state(void* instance, const void* state_buffer,
                             size_t buffer_size) -> PeripheralStatus_t {
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size != sizeof(JoystickSaveState_t)) {
    return peripheral_error;
  }
  JoystickSaveState_t state{};
  std::memcpy(&state, state_buffer, sizeof(state));
  if (state.version != JOYSTICK_STATE_VERSION ||
      state.struct_size != sizeof(state)) {
    return peripheral_error;
  }

  auto* port = static_cast<GamePort_t*>(instance);
  for (size_t paddle = 0; paddle < paddle_count; ++paddle) {
    port->trigger_cycle.at(paddle) = state.trigger_cycle[paddle];
    port->pulse_position.at(paddle) = port->position.at(paddle);
  }
  return peripheral_ok;
}

}  // namespace

static Peripheral_t joystick_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.joystick",
    .name = "Joystick",
    .description = "Apple II game I/O port: four paddle timers",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_INTERNAL,
    .default_slot = 0,
    .init = joystick_abi_init,
    // RESET' does not reach the NE558 (its RESET pin is unused, Sather 7-11).
    .reset = nullptr,
    .shutdown = joystick_abi_shutdown,
    .think = nullptr,
    .on_vblank = nullptr,
    .save_state = joystick_abi_save_state,
    .load_state = joystick_abi_load_state,
    .command = joystick_abi_command,
    .query = joystick_abi_query,
};

// Peripheral registry requires non-const pointer.
auto joystick_get_descriptor() -> Peripheral_t* { return &joystick_peripheral; }

PERIPHERAL_REGISTER(joystick_peripheral)
