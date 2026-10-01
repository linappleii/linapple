// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/joystick/Joystick.h"

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

// The "6" line of the I/O selector enables a 74LS251 that puts one of eight
// inputs on bit 7 of the data bus alone: the three pushbuttons at $C061-$C063
// and the four NE558 timer outputs at $C064-$C067. Bits 0-6 are whatever the
// undriven bus holds (Apple II Reference Manual, 1979, p. 99; Apple IIe
// Technical Reference Manual, p. 41). The "7" line, any access to $C070-$C07F,
// triggers all four timers (1979 manual p. 99; IIe Tech Ref p. 187).
constexpr uint16_t addr_switch0 = 0xC061;
constexpr uint16_t addr_switch2 = 0xC063;
constexpr uint16_t addr_paddle0 = 0xC064;
constexpr uint16_t addr_paddle3 = 0xC067;
constexpr uint16_t addr_trigger = 0xC070;
constexpr uint16_t paddle_select_mask = 0x03;
constexpr uint8_t bus_data_mask = 0x7F;
constexpr uint8_t input_bit = 0x80;

constexpr size_t paddle_count = 4;
constexpr size_t switch_count = 3;
constexpr uint8_t joystick_count = 2;
constexpr uint8_t axis_count = 2;
constexpr uint8_t level_max = 1;
constexpr uint8_t source_max = 1;

// Halfway along the pot's travel, where a centred stick rests.
constexpr uint8_t centre_position = 127;

// PREAD, at $FB1E in every monitor ROM under res/roms, strobes $C070 and then
// LDY #0, NOP, NOP and the fourth cycle of LDA $C064,X put its first sample
// ten cycles after the strobe; its loop (LDA 4, BPL 2, INY 2, BNE 3) samples
// every eleven cycles after that (Sather, Understanding the Apple II, 7-24).
// A pulse of 11 x position + 10 cycles therefore makes PREAD return exactly
// position. This is a count model: position 255 is a 2,815-cycle pulse, not
// the 3,370-cycle pulse of a 150 kOhm pot at full travel (Sather 7-11), so a
// program that polls faster than PREAD sees the top of the travel compressed.
constexpr uint64_t pulse_cycles_per_count = 11;
constexpr uint64_t pulse_lead_in_cycles = 10;

// A pressed switch is held for about 10 ms of emulated time so that a host
// press and release delivered in the same batch of events still reach the
// 6502 as a press (1,020,484 cycles per second, Sather 3-3).
constexpr uint64_t button_hold_cycles = 10205;

struct GamePort_t {
  std::array<uint64_t, paddle_count> trigger_cycle{};
  std::array<uint8_t, paddle_count> position{
      {centre_position, centre_position, centre_position, centre_position}};
  std::array<bool, switch_count> buttons{};
  std::array<uint64_t, switch_count> button_hold{};
  HostInterface_t* host = nullptr;
  int slot = 0;
};

auto pulse_cycles(uint8_t position) -> uint64_t {
  return (static_cast<uint64_t>(position) * pulse_cycles_per_count) +
         pulse_lead_in_cycles;
}

auto joystick_io_read_switch(void* instance, uint16_t program_counter,
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
  if (memory_address < addr_switch0 || memory_address > addr_switch2) {
    return result;
  }
  const size_t line = memory_address - addr_switch0;
  if (port->buttons.at(line) || port->button_hold.at(line) > 0) {
    result |= input_bit;
  }
  return result;
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
  const size_t paddle = memory_address & paddle_select_mask;
  const uint64_t now = port->host->GetCycles();
  const uint64_t trigger = port->trigger_cycle.at(paddle);
  const uint64_t elapsed = now >= trigger ? now - trigger : 0;
  if (elapsed < pulse_cycles(port->position.at(paddle))) {
    result |= input_bit;
  }
  return result;
}

auto joystick_io_trigger(void* instance, uint16_t program_counter,
                         uint16_t memory_address, uint8_t is_write,
                         uint8_t data_value, uint32_t executed_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)memory_address;
  (void)is_write;
  (void)data_value;
  if (instance == nullptr) {
    return 0;
  }
  auto* port = static_cast<GamePort_t*>(instance);
  port->trigger_cycle.fill(port->host->GetCycles());
  return port->host->ReadFloatingBus(executed_cycles);
}

// Without the I/O members the 6502 never reaches the port, without the cycle
// counter every timer reads as charging for ever, and without the bus a read
// has no low seven bits to return: better no card than a phantom one, and the
// log says which member was missing.
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

  for (uint16_t addr = addr_switch0; addr <= addr_switch2; ++addr) {
    host->RegisterDirectIO(port.get(), addr, joystick_io_read_switch, nullptr);
  }
  for (uint16_t addr = addr_paddle0; addr <= addr_paddle3; ++addr) {
    host->RegisterDirectIO(port.get(), addr, joystick_io_read_paddle, nullptr);
  }
  host->RegisterDirectIO(port.get(), addr_trigger, joystick_io_trigger,
                         joystick_io_trigger);

  return port.release();
}

auto joystick_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* port = static_cast<GamePort_t*>(instance);
  port->trigger_cycle.fill(0);
  port->position.fill(centre_position);
  port->buttons.fill(false);
  port->button_hold.fill(0);
}

auto joystick_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<GamePort_t> port(static_cast<GamePort_t*>(instance));
}

auto joystick_abi_think(void* instance, uint32_t elapsed_cycles) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* port = static_cast<GamePort_t*>(instance);
  for (auto& hold : port->button_hold) {
    hold = hold > elapsed_cycles ? hold - elapsed_cycles : 0;
  }
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

  switch (command_id) {
    case JOYSTICK_CMD_SET_AXIS: {
      if (payload == nullptr || payload_size != sizeof(JoystickAxisPayload_t)) {
        return peripheral_error;
      }
      const auto* axis = static_cast<const JoystickAxisPayload_t*>(payload);
      if (axis->joystick >= joystick_count || axis->axis >= axis_count) {
        return peripheral_error;
      }
      const size_t paddle =
          (static_cast<size_t>(axis->joystick) * axis_count) + axis->axis;
      port->position.at(paddle) = axis->value;
      return peripheral_ok;
    }
    case JOYSTICK_CMD_SET_BUTTON: {
      if (payload == nullptr ||
          payload_size != sizeof(JoystickButtonPayload_t)) {
        return peripheral_error;
      }
      const auto* button = static_cast<const JoystickButtonPayload_t*>(payload);
      if (button->button >= switch_count || button->down > level_max ||
          button->source > source_max) {
        return peripheral_error;
      }
      const bool down = button->down != 0;
      if (down && !port->buttons.at(button->button)) {
        port->button_hold.at(button->button) = button_hold_cycles;
      }
      port->buttons.at(button->button) = down;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

// The port answers no query: its state is read through the switches and the
// timers, and the host keeps its own input mapping.
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
  for (size_t joystick = 0; joystick < joystick_count; ++joystick) {
    state.x_pos[joystick] = port->position.at(joystick * axis_count);
    state.y_pos[joystick] = port->position.at((joystick * axis_count) + 1);
  }
  for (size_t line = 0; line < switch_count; ++line) {
    state.buttons[line] = port->buttons.at(line) ? 1 : 0;
  }
  std::memcpy(state_buffer, &state, required_size);

  *buffer_size = required_size;
  return peripheral_ok;
}

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
  for (size_t line = 0; line < switch_count; ++line) {
    if (state.buttons[line] > level_max) {
      return peripheral_error;
    }
  }

  auto* port = static_cast<GamePort_t*>(instance);
  for (size_t paddle = 0; paddle < paddle_count; ++paddle) {
    port->trigger_cycle.at(paddle) = state.trigger_cycle[paddle];
  }
  for (size_t joystick = 0; joystick < joystick_count; ++joystick) {
    port->position.at(joystick * axis_count) = state.x_pos[joystick];
    port->position.at((joystick * axis_count) + 1) = state.y_pos[joystick];
  }
  for (size_t line = 0; line < switch_count; ++line) {
    port->buttons.at(line) = state.buttons[line] != 0;
  }
  return peripheral_ok;
}

}  // namespace

static const Peripheral_t joystick_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.joystick",
    .name = "Joystick",
    .description =
        "Apple II game I/O port: four paddle timers and three "
        "pushbutton inputs",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_INTERNAL,
    .default_slot = 0,
    .init = joystick_abi_init,
    .reset = joystick_abi_reset,
    .shutdown = joystick_abi_shutdown,
    .think = joystick_abi_think,
    .on_vblank = nullptr,
    .save_state = joystick_abi_save_state,
    .load_state = joystick_abi_load_state,
    .command = joystick_abi_command,
    .query = joystick_abi_query};

// peripheral_register and ActivePeripheral_t::api take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
auto joystick_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&joystick_peripheral);
}

PERIPHERAL_REGISTER(joystick_peripheral)
