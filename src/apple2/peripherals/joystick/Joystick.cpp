// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/joystick/Joystick.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

constexpr uint64_t BUTTON_LATCH_CYCLES = 10205;
constexpr uint8_t joy_default_pos = 127;
constexpr uint8_t joy_bit_mask = 0x7F;
constexpr uint8_t joy_high_bit = 0x80;

constexpr uint16_t addr_button0 = 0xC061;
constexpr uint16_t addr_button2 = 0xC063;
constexpr uint16_t addr_paddle0 = 0xC064;
constexpr uint16_t addr_paddle3 = 0xC067;
constexpr uint16_t addr_paddle_reset = 0xC070;

constexpr uint32_t paddle_timing_multiplier = 11;
constexpr uint32_t paddle_timing_offset = 10;

constexpr int joystick_count = 2;
constexpr int joystick_button_count = 3;

static auto get_cycles(HostInterface_t* host) -> uint64_t {
  if (host != nullptr && host->GetCycles != nullptr) {
    return host->GetCycles();
  }
  return 0;
}

struct JoystickPeripheral_t {
  uint64_t reset_cycle = 0;
  std::array<uint8_t, joystick_count> x_pos{joy_default_pos, joy_default_pos};
  std::array<uint8_t, joystick_count> y_pos{joy_default_pos, joy_default_pos};
  std::array<bool, joystick_button_count> buttons{false, false, false};
  std::array<uint64_t, joystick_button_count> button_latches{0, 0, 0};
  int16_t trim_x = 0;
  int16_t trim_y = 0;

  JoystickConfig_t config{};
  bool has_quit_event = false;

  HostInterface_t* host = nullptr;
  int slot = 0;

  JoystickPeripheral_t() = default;
};

static auto joy_io_read_button(void* instance, uint16_t program_counter,
                               uint16_t memory_address, uint8_t is_write,
                               uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)is_write;
  (void)data_value;

  if (instance == nullptr) {
    return mem_read_floating_bus(remaining_cycles);
  }

  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  uint8_t result = mem_read_floating_bus(remaining_cycles) & joy_bit_mask;
  const int button_index = static_cast<int>(memory_address - addr_button0);

  if (button_index >= 0 && button_index < joystick_button_count) {
    const bool is_pressed = joystick_peripheral->buttons.at(button_index);
    const bool is_latched =
        joystick_peripheral->button_latches.at(button_index) > 0;

    if (is_pressed || is_latched) {
      result |= joy_high_bit;
    }
  }

  return result;
}

static auto joy_io_read_position(void* instance, uint16_t program_counter,
                                 uint16_t memory_address, uint8_t is_write,
                                 uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)is_write;
  (void)data_value;

  if (instance == nullptr) {
    return mem_read_floating_bus(remaining_cycles);
  }

  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);
  uint8_t result = mem_read_floating_bus(remaining_cycles) & joy_bit_mask;

  const int paddle_index = static_cast<int>(memory_address & 0x03);
  const int joystick_index = paddle_index >> 1;
  const bool is_y_axis = (paddle_index & 1) != 0;

  const uint8_t raw_position =
      is_y_axis ? joystick_peripheral->y_pos.at(joystick_index)
                : joystick_peripheral->x_pos.at(joystick_index);

  int16_t trimmed_position =
      static_cast<int16_t>(raw_position) +
      (is_y_axis ? joystick_peripheral->trim_y : joystick_peripheral->trim_x);

  if (trimmed_position < 0) {
    trimmed_position = 0;
  } else if (trimmed_position > 255) {
    trimmed_position = 255;
  }

  const uint64_t current_cycle = get_cycles(joystick_peripheral->host);
  const uint64_t elapsed_cycles =
      (current_cycle >= joystick_peripheral->reset_cycle)
          ? (current_cycle - joystick_peripheral->reset_cycle)
          : 0;
  const uint64_t charge_limit =
      (static_cast<uint64_t>(trimmed_position) * paddle_timing_multiplier) +
      paddle_timing_offset;

  if (elapsed_cycles < charge_limit) {
    result |= joy_high_bit;
  }

  return result;
}

static auto joy_io_reset_position(void* instance, uint16_t program_counter,
                                  uint16_t memory_address, uint8_t is_write,
                                  uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)memory_address;
  (void)is_write;
  (void)data_value;

  if (instance == nullptr) {
    return mem_read_floating_bus(remaining_cycles);
  }

  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);
  joystick_peripheral->reset_cycle = get_cycles(joystick_peripheral->host);

  return mem_read_floating_bus(remaining_cycles);
}

static auto joystick_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  auto joystick_peripheral =
      std::unique_ptr<JoystickPeripheral_t>(new JoystickPeripheral_t());
  joystick_peripheral->host = host;
  joystick_peripheral->slot = slot;

  if (host->RegisterDirectIO != nullptr) {
    for (uint16_t addr = addr_button0; addr <= addr_button2; ++addr) {
      host->RegisterDirectIO(joystick_peripheral.get(), addr,
                             joy_io_read_button, nullptr);
    }

    for (uint16_t addr = addr_paddle0; addr <= addr_paddle3; ++addr) {
      host->RegisterDirectIO(joystick_peripheral.get(), addr,
                             joy_io_read_position, nullptr);
    }

    host->RegisterDirectIO(joystick_peripheral.get(), addr_paddle_reset,
                           joy_io_reset_position, joy_io_reset_position);
  }

  return joystick_peripheral.release();
}

static auto joystick_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  joystick_peripheral->button_latches.fill(0);
  joystick_peripheral->buttons.fill(false);
  joystick_peripheral->x_pos.fill(joy_default_pos);
  joystick_peripheral->y_pos.fill(joy_default_pos);
  joystick_peripheral->reset_cycle = 0;
}

static auto joystick_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<JoystickPeripheral_t> joystick_peripheral(
      static_cast<JoystickPeripheral_t*>(instance));
}

static auto joystick_abi_think(void* instance, uint32_t cycles) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  for (auto& latch : joystick_peripheral->button_latches) {
    if (latch > cycles) {
      latch -= cycles;
    } else {
      latch = 0;
    }
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
// Justification: ABI-required function signature.
static auto joystick_abi_command(void* instance, uint32_t cmd, const void* data,
                                 size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  switch (cmd) {
    case JOY_CMD_SET_AXIS: {
      if (data == nullptr || size < sizeof(JoystickAxisPayload_t)) {
        return peripheral_error;
      }
      const auto* payload = static_cast<const JoystickAxisPayload_t*>(data);
      if (payload->joystick >= joystick_count || payload->axis > 1) {
        return peripheral_error;
      }
      if (payload->axis == 0) {
        joystick_peripheral->x_pos.at(payload->joystick) = payload->value;
      } else {
        joystick_peripheral->y_pos.at(payload->joystick) = payload->value;
      }
      return peripheral_ok;
    }
    case JOY_CMD_SET_BUTTON: {
      if (data == nullptr || size < sizeof(JoystickButtonPayload_t)) {
        return peripheral_error;
      }
      const auto* payload = static_cast<const JoystickButtonPayload_t*>(data);
      if (payload->button >= joystick_button_count) {
        return peripheral_error;
      }
      if (payload->down && !joystick_peripheral->buttons.at(payload->button)) {
        joystick_peripheral->button_latches.at(payload->button) =
            BUTTON_LATCH_CYCLES;
      }
      joystick_peripheral->buttons.at(payload->button) = payload->down;
      return peripheral_ok;
    }
    case JOY_CMD_SET_TRIM: {
      if (data == nullptr || size < sizeof(JoystickTrimPayload_t)) {
        return peripheral_error;
      }
      const auto* payload = static_cast<const JoystickTrimPayload_t*>(data);
      if (payload->axis_x) {
        joystick_peripheral->trim_x = payload->value;
      } else {
        joystick_peripheral->trim_y = payload->value;
      }
      return peripheral_ok;
    }
    case JOY_CMD_RESET: {
      joystick_abi_reset(instance);
      return peripheral_ok;
    }
    case JOY_CMD_SET_CONFIG: {
      if (data == nullptr || size < sizeof(JoystickConfig_t)) {
        return peripheral_error;
      }
      std::memcpy(&joystick_peripheral->config, data, sizeof(JoystickConfig_t));
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
// Justification: ABI-required function signature.
static auto joystick_abi_query(void* instance, uint32_t cmd, void* out,
                               size_t* size) -> PeripheralStatus_t {
  if (size == nullptr) {
    return peripheral_error;
  }

  switch (cmd) {
    case JOY_QUERY_CONFIG: {
      constexpr size_t required_size = sizeof(JoystickConfig_t);
      if (out == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (instance == nullptr || *size < required_size) {
        return peripheral_error;
      }
      const auto* joystick_peripheral =
          static_cast<const JoystickPeripheral_t*>(instance);
      std::memcpy(out, &joystick_peripheral->config, required_size);
      *size = required_size;
      return peripheral_ok;
    }
    case JOY_QUERY_EXIT_EVENT: {
      constexpr size_t required_size = sizeof(uint8_t);
      if (out == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (instance == nullptr || *size < required_size) {
        return peripheral_error;
      }
      const auto* joystick_peripheral =
          static_cast<const JoystickPeripheral_t*>(instance);
      *static_cast<uint8_t*>(out) = joystick_peripheral->has_quit_event ? 1 : 0;
      *size = required_size;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
// Justification: ABI-required function signature.
static auto joystick_abi_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  if (size == nullptr) {
    return peripheral_error;
  }

  constexpr size_t required_size = sizeof(JoystickSaveState_t);
  if (buffer == nullptr) {
    *size = required_size;
    return peripheral_ok;
  }

  if (instance == nullptr || *size < required_size) {
    return peripheral_error;
  }

  const auto* joystick_peripheral =
      static_cast<const JoystickPeripheral_t*>(instance);
  auto* ss = static_cast<JoystickSaveState_t*>(buffer);
  std::memset(ss, 0, sizeof(JoystickSaveState_t));
  ss->version = JOYSTICK_STATE_VERSION;
  ss->struct_size = static_cast<uint32_t>(required_size);
  ss->reset_cycle = joystick_peripheral->reset_cycle;
  for (size_t i = 0; i < 3; ++i) {
    ss->button_latches[i] = joystick_peripheral->button_latches.at(i);
    ss->buttons[i] = joystick_peripheral->buttons.at(i) ? 1 : 0;
  }
  for (size_t i = 0; i < 2; ++i) {
    ss->x_pos[i] = joystick_peripheral->x_pos.at(i);
    ss->y_pos[i] = joystick_peripheral->y_pos.at(i);
  }
  ss->trim_x = joystick_peripheral->trim_x;
  ss->trim_y = joystick_peripheral->trim_y;

  *size = required_size;
  return peripheral_ok;
}

static auto joystick_abi_load_state(void* instance, const void* buffer,
                                    size_t size) -> PeripheralStatus_t {
  if (instance == nullptr || buffer == nullptr ||
      size != sizeof(JoystickSaveState_t)) {
    return peripheral_error;
  }

  const auto* ss = static_cast<const JoystickSaveState_t*>(buffer);
  if (ss->version != JOYSTICK_STATE_VERSION ||
      ss->struct_size != sizeof(JoystickSaveState_t)) {
    return peripheral_error;
  }

  for (size_t i = 0; i < 3; ++i) {
    if (ss->buttons[i] > 1) {
      return peripheral_error;
    }
  }

  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);
  joystick_peripheral->reset_cycle = ss->reset_cycle;
  for (size_t i = 0; i < 3; ++i) {
    joystick_peripheral->button_latches.at(i) = ss->button_latches[i];
    joystick_peripheral->buttons.at(i) = (ss->buttons[i] != 0);
  }
  for (size_t i = 0; i < 2; ++i) {
    joystick_peripheral->x_pos.at(i) = ss->x_pos[i];
    joystick_peripheral->y_pos.at(i) = ss->y_pos[i];
  }
  joystick_peripheral->trim_x = ss->trim_x;
  joystick_peripheral->trim_y = ss->trim_y;

  return peripheral_ok;
}

}  // namespace

static Peripheral_t g_joystick_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.joystick",
    .name = "Joystick",
    .description = "Analog joystick and paddle peripheral",
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

auto joystick_get_descriptor() -> Peripheral_t* {
  return &g_joystick_peripheral;
}

PERIPHERAL_REGISTER(g_joystick_peripheral)
