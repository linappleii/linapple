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
#include "apple2/Structs.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/Common_Globals.h"
#include "core/Peripheral.h"

namespace {

constexpr double LATCH_DURATION_SECONDS = 0.01;
constexpr uint8_t JOY_DEFAULT_POS = 127;
constexpr uint8_t JOY_BIT_MASK = 0x7F;
constexpr uint8_t JOY_HIGH_BIT = 0x80;

constexpr uint16_t ADDR_BUTTON0 = 0xC061;
constexpr uint16_t ADDR_BUTTON2 = 0xC063;
constexpr uint16_t ADDR_PADDLE0 = 0xC064;
constexpr uint16_t ADDR_PADDLE3 = 0xC067;
constexpr uint16_t ADDR_PADDLE_RESET = 0xC070;

constexpr uint32_t PADDLE_TIMING_MULTIPLIER = 11;
constexpr uint32_t PADDLE_TIMING_OFFSET = 10;

constexpr int JOYSTICK_COUNT = 2;
constexpr int JOYSTICK_BUTTON_COUNT = 3;

static auto GetCycles(HostInterface_t* host) -> uint64_t {
  if (host != nullptr && host->GetCycles != nullptr) {
    return host->GetCycles();
  }
  return CpuGetCumulativeCycles();
}

auto GetButtonLatchDuration() -> uint64_t {
  return static_cast<uint64_t>(g_fCurrentCLK6502 * LATCH_DURATION_SECONDS);
}

struct JoystickPeripheral_t {
  uint64_t reset_cycle = 0;
  std::array<uint8_t, JOYSTICK_COUNT> x_pos{JOY_DEFAULT_POS, JOY_DEFAULT_POS};
  std::array<uint8_t, JOYSTICK_COUNT> y_pos{JOY_DEFAULT_POS, JOY_DEFAULT_POS};
  std::array<bool, JOYSTICK_BUTTON_COUNT> buttons{false, false, false};
  std::array<uint64_t, JOYSTICK_BUTTON_COUNT> button_latches{0, 0, 0};
  int16_t trim_x = 0;
  int16_t trim_y = 0;

  JoystickConfig_t config{};
  bool has_quit_event = false;

  HostInterface_t* host = nullptr;
  int slot = 0;

  JoystickPeripheral_t() = default;
};

auto Joy_IO_ReadButton(void* instance, uint16_t program_counter,
                       uint16_t memory_address, uint8_t is_write,
                       uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)is_write;
  (void)data_value;

  if (instance == nullptr) {
    return MemReadFloatingBus(remaining_cycles);
  }

  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  uint8_t result = MemReadFloatingBus(remaining_cycles) & JOY_BIT_MASK;
  const int button_index = static_cast<int>(memory_address - ADDR_BUTTON0);

  if (button_index >= 0 && button_index < JOYSTICK_BUTTON_COUNT) {
    const bool is_pressed = joystick_peripheral->buttons.at(button_index);
    const bool is_latched =
        joystick_peripheral->button_latches.at(button_index) > 0;

    if (is_pressed || is_latched) {
      result |= JOY_HIGH_BIT;
    }
  }

  return result;
}

auto Joy_IO_ReadPosition(void* instance, uint16_t program_counter,
                         uint16_t memory_address, uint8_t is_write,
                         uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)is_write;
  (void)data_value;

  if (instance == nullptr) {
    return MemReadFloatingBus(remaining_cycles);
  }

  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);
  uint8_t result = MemReadFloatingBus(remaining_cycles) & JOY_BIT_MASK;

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
      GetCycles(joystick_peripheral->host) - joystick_peripheral->reset_cycle;
  const uint64_t charge_limit =
      (static_cast<uint64_t>(trimmed_position) * PADDLE_TIMING_MULTIPLIER) +
      PADDLE_TIMING_OFFSET;

  if (elapsed_cycles < charge_limit) {
    result |= JOY_HIGH_BIT;
  }

  return result;
}

auto Joy_IO_ResetPosition(void* instance, uint16_t program_counter,
                          uint16_t memory_address, uint8_t is_write,
                          uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)memory_address;
  (void)is_write;
  (void)data_value;

  if (instance == nullptr) {
    return MemReadFloatingBus(remaining_cycles);
  }

  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  CpuCalcCycles(remaining_cycles);
  joystick_peripheral->reset_cycle = GetCycles(joystick_peripheral->host);

  return MemReadFloatingBus(remaining_cycles);
}

static auto Joystick_ABI_Init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  auto joystick_peripheral =
      std::unique_ptr<JoystickPeripheral_t>(new JoystickPeripheral_t());
  joystick_peripheral->host = host;
  joystick_peripheral->slot = slot;

  for (uint16_t addr = ADDR_BUTTON0; addr <= ADDR_BUTTON2; ++addr) {
    host->RegisterDirectIO(joystick_peripheral.get(), addr, Joy_IO_ReadButton,
                           nullptr);
  }

  for (uint16_t addr = ADDR_PADDLE0; addr <= ADDR_PADDLE3; ++addr) {
    host->RegisterDirectIO(joystick_peripheral.get(), addr, Joy_IO_ReadPosition,
                           nullptr);
  }

  host->RegisterDirectIO(joystick_peripheral.get(), ADDR_PADDLE_RESET,
                         Joy_IO_ResetPosition, Joy_IO_ResetPosition);

  return joystick_peripheral.release();
}

static auto Joystick_ABI_Reset(void* instance) -> void {
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
    x = JOY_DEFAULT_POS;
  }
  for (auto& y : joystick_peripheral->y_pos) {
    y = JOY_DEFAULT_POS;
  }
  joystick_peripheral->reset_cycle = 0;
}

static auto Joystick_ABI_Shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<JoystickPeripheral_t> joystick_peripheral(
      static_cast<JoystickPeripheral_t*>(instance));
}

static auto Joystick_ABI_Think(void* instance, uint32_t cycles) -> void {
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

static auto Joystick_ABI_Command(void* instance, uint32_t cmd, const void* data,
                                 size_t size) -> PeripheralStatus {
  if (instance == nullptr) {
    return PERIPHERAL_ERROR;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  switch (static_cast<JoystickCmd_e>(cmd)) {
    case JOY_CMD_SET_AXIS: {
      if (size < sizeof(JoystickAxisPayload_t)) {
        return PERIPHERAL_ERROR;
      }
      const auto* payload = static_cast<const JoystickAxisPayload_t*>(data);
      if (payload->joystick < JOYSTICK_COUNT) {
        if (payload->axis == 0) {
          joystick_peripheral->x_pos.at(payload->joystick) = payload->value;
        } else {
          joystick_peripheral->y_pos.at(payload->joystick) = payload->value;
        }
      }
      return PERIPHERAL_OK;
    }
    case JOY_CMD_SET_BUTTON: {
      if (size < sizeof(JoystickButtonPayload_t)) {
        return PERIPHERAL_ERROR;
      }
      const auto* payload = static_cast<const JoystickButtonPayload_t*>(data);
      if (payload->button < JOYSTICK_BUTTON_COUNT) {
        if (payload->down &&
            !joystick_peripheral->buttons.at(payload->button)) {
          joystick_peripheral->button_latches.at(payload->button) =
              GetButtonLatchDuration();
        }
        joystick_peripheral->buttons.at(payload->button) = payload->down;
      }
      return PERIPHERAL_OK;
    }
    case JOY_CMD_SET_TRIM: {
      if (size < sizeof(JoystickTrimPayload_t)) {
        return PERIPHERAL_ERROR;
      }
      const auto* payload = static_cast<const JoystickTrimPayload_t*>(data);
      if (payload->axis_x) {
        joystick_peripheral->trim_x = payload->value;
      } else {
        joystick_peripheral->trim_y = payload->value;
      }
      return PERIPHERAL_OK;
    }
    case JOY_CMD_RESET: {
      Joystick_ABI_Reset(instance);
      return PERIPHERAL_OK;
    }
    case JOY_CMD_SET_CONFIG: {
      if (size < sizeof(JoystickConfig_t)) {
        return PERIPHERAL_ERROR;
      }
      std::copy_n(static_cast<const uint8_t*>(data), sizeof(JoystickConfig_t),
                  reinterpret_cast<uint8_t*>(&joystick_peripheral->config));
      return PERIPHERAL_OK;
    }
    default:
      break;
  }
  return PERIPHERAL_ERROR;
}

static auto Joystick_ABI_Query(void* instance, uint32_t cmd, void* out,
                               size_t* size) -> PeripheralStatus {
  if (instance == nullptr || out == nullptr || size == nullptr) {
    return PERIPHERAL_ERROR;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  switch (static_cast<JoystickQuery_e>(cmd)) {
    case JOY_QUERY_CONFIG: {
      if (*size < sizeof(JoystickConfig_t)) {
        return PERIPHERAL_ERROR;
      }
      std::copy_n(
          reinterpret_cast<const uint8_t*>(&joystick_peripheral->config),
          sizeof(JoystickConfig_t), static_cast<uint8_t*>(out));
      *size = sizeof(JoystickConfig_t);
      return PERIPHERAL_OK;
    }
    case JOY_QUERY_EXIT_EVENT: {
      if (*size < 1) {
        return PERIPHERAL_ERROR;
      }
      *static_cast<uint8_t*>(out) = joystick_peripheral->has_quit_event ? 1 : 0;
      *size = 1;
      return PERIPHERAL_OK;
    }
    default:
      break;
  }
  return PERIPHERAL_ERROR;
}

static auto Joystick_ABI_SaveState(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus {
  if (instance == nullptr || size == nullptr) {
    return PERIPHERAL_ERROR;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);

  if (buffer == nullptr) {
    *size = sizeof(SS_IO_Joystick);
    return PERIPHERAL_OK;
  }

  if (*size < sizeof(SS_IO_Joystick)) {
    return PERIPHERAL_ERROR;
  }

  auto* ss = static_cast<SS_IO_Joystick*>(buffer);
  ss->g_nJoyCntrResetCycle = joystick_peripheral->reset_cycle;

  *size = sizeof(SS_IO_Joystick);
  return PERIPHERAL_OK;
}

static auto Joystick_ABI_LoadState(void* instance, const void* buffer,
                                   size_t size) -> PeripheralStatus {
  if (instance == nullptr || buffer == nullptr ||
      size < sizeof(SS_IO_Joystick)) {
    return PERIPHERAL_ERROR;
  }
  auto* joystick_peripheral = static_cast<JoystickPeripheral_t*>(instance);
  const auto* ss = static_cast<const SS_IO_Joystick*>(buffer);

  joystick_peripheral->reset_cycle = ss->g_nJoyCntrResetCycle;

  return PERIPHERAL_OK;
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
    .init = Joystick_ABI_Init,
    .reset = Joystick_ABI_Reset,
    .shutdown = Joystick_ABI_Shutdown,
    .think = Joystick_ABI_Think,
    .on_vblank = nullptr,
    .save_state = Joystick_ABI_SaveState,
    .load_state = Joystick_ABI_LoadState,
    .command = Joystick_ABI_Command,
    .query = Joystick_ABI_Query};

auto Joystick_GetDescriptor() -> Peripheral_t* {
  return &g_joystick_peripheral;
}

PERIPHERAL_REGISTER(g_joystick_peripheral)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           cppcoreguidelines-owning-memory)
