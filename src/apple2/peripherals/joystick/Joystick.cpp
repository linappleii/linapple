// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//             cppcoreguidelines-owning-memory)
#include "apple2/peripherals/joystick/Joystick.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/Peripheral.h"

namespace {

constexpr double latch_duration_seconds = 0.01;
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
  return cpu_get_cumulative_cycles();
}

auto get_button_latch_duration() -> uint64_t {
  return static_cast<uint64_t>(g_current_clk_6502 * latch_duration_seconds);
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

auto joy_io_read_button(void* instance, uint16_t program_counter,
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

auto joy_io_read_position(void* instance, uint16_t program_counter,
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

  const uint64_t elapsed_cycles =
      get_cycles(joystick_peripheral->host) - joystick_peripheral->reset_cycle;
  const uint64_t charge_limit =
      (static_cast<uint64_t>(trimmed_position) * paddle_timing_multiplier) +
      paddle_timing_offset;

  if (elapsed_cycles < charge_limit) {
    result |= joy_high_bit;
  }

  return result;
}

auto joy_io_reset_position(void* instance, uint16_t program_counter,
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

  cpu_calc_cycles(remaining_cycles);
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

  for (uint16_t addr = addr_button0; addr <= addr_button2; ++addr) {
    host->RegisterDirectIO(joystick_peripheral.get(), addr, joy_io_read_button,
                           nullptr);
  }

  for (uint16_t addr = addr_paddle0; addr <= addr_paddle3; ++addr) {
    host->RegisterDirectIO(joystick_peripheral.get(), addr,
                           joy_io_read_position, nullptr);
  }

  host->RegisterDirectIO(joystick_peripheral.get(), addr_paddle_reset,
                         joy_io_reset_position, joy_io_reset_position);

  return joystick_peripheral.release();
}

static auto joystick_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  for (auto& latch : joystick_peripheral->button_latches) {
    latch = 0;
  }
  for (auto& button : joystick_peripheral->buttons) {
    button = false;
  }
  for (auto& x : joystick_peripheral->x_pos) {
    x = joy_default_pos;
  }
  for (auto& y : joystick_peripheral->y_pos) {
    y = joy_default_pos;
  }
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

static auto joystick_abi_command(void* instance, uint32_t cmd, const void* data,
                                 size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  switch (static_cast<JoystickCmd_e>(cmd)) {
    case JOY_CMD_SET_AXIS: {
      if (size < sizeof(JoystickAxisPayload_t)) {
        return peripheral_error;
      }
      const auto* payload = static_cast<const JoystickAxisPayload_t*>(data);
      if (payload->joystick < joystick_count) {
        if (payload->axis == 0) {
          joystick_peripheral->x_pos.at(payload->joystick) = payload->value;
        } else {
          joystick_peripheral->y_pos.at(payload->joystick) = payload->value;
        }
      }
      return peripheral_ok;
    }
    case JOY_CMD_SET_BUTTON: {
      if (size < sizeof(JoystickButtonPayload_t)) {
        return peripheral_error;
      }
      const auto* payload = static_cast<const JoystickButtonPayload_t*>(data);
      if (payload->button < joystick_button_count) {
        if (payload->down &&
            !joystick_peripheral->buttons.at(payload->button)) {
          joystick_peripheral->button_latches.at(payload->button) =
              get_button_latch_duration();
        }
        joystick_peripheral->buttons.at(payload->button) = payload->down;
      }
      return peripheral_ok;
    }
    case JOY_CMD_SET_TRIM: {
      if (size < sizeof(JoystickTrimPayload_t)) {
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
      if (size < sizeof(JoystickConfig_t)) {
        return peripheral_error;
      }
      std::copy_n(static_cast<const uint8_t*>(data), sizeof(JoystickConfig_t),
                  reinterpret_cast<uint8_t*>(&joystick_peripheral->config));
      return peripheral_ok;
    }
    default:
      break;
  }
  return peripheral_error;
}

static auto joystick_abi_query(void* instance, uint32_t cmd, void* out,
                               size_t* size) -> PeripheralStatus_t {
  if (instance == nullptr || out == nullptr || size == nullptr) {
    return peripheral_error;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  switch (static_cast<JoystickQuery_e>(cmd)) {
    case JOY_QUERY_CONFIG: {
      if (*size < sizeof(JoystickConfig_t)) {
        return peripheral_error;
      }
      std::copy_n(
          reinterpret_cast<const uint8_t*>(&joystick_peripheral->config),
          sizeof(JoystickConfig_t), static_cast<uint8_t*>(out));
      *size = sizeof(JoystickConfig_t);
      return peripheral_ok;
    }
    case JOY_QUERY_EXIT_EVENT: {
      if (*size < 1) {
        return peripheral_error;
      }
      *static_cast<uint8_t*>(out) = joystick_peripheral->has_quit_event ? 1 : 0;
      *size = 1;
      return peripheral_ok;
    }
    default:
      break;
  }
  return peripheral_error;
}

static auto joystick_abi_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  if (instance == nullptr || size == nullptr) {
    return peripheral_error;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  if (buffer == nullptr) {
    *size = sizeof(SS_IO_Joystick);
    return peripheral_ok;
  }

  if (*size < sizeof(SS_IO_Joystick)) {
    return peripheral_error;
  }

  auto* ss = static_cast<SS_IO_Joystick*>(buffer);
  ss->joy_cntr_reset_cycle = joystick_peripheral->reset_cycle;

  *size = sizeof(SS_IO_Joystick);
  return peripheral_ok;
}

static auto joystick_abi_load_state(void* instance, const void* buffer,
                                    size_t size) -> PeripheralStatus_t {
  if (instance == nullptr || buffer == nullptr ||
      size < sizeof(SS_IO_Joystick)) {
    return peripheral_error;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);
  const auto* ss = static_cast<const SS_IO_Joystick*>(buffer);

  joystick_peripheral->reset_cycle = ss->joy_cntr_reset_cycle;

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
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           cppcoreguidelines-owning-memory)
