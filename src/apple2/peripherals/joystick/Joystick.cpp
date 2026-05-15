/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Joystick and paddle emulation
 *
 * Author: Michael O'Brien, modified for decoupling.
 */

#include "apple2/peripherals/joystick/Joystick.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Structs.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/Log.h"
#include "core/Peripheral.h"

// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables)

namespace {
constexpr double BUTTON_TIME_DIVISOR = 100.0;
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
}  // namespace

static inline auto GetButtonTime() -> uint64_t {
  return static_cast<uint64_t>(g_fCurrentCLK6502 / BUTTON_TIME_DIVISOR);
}

struct JoystickPeripheral_t {
  uint64_t reset_cycle = 0;
  uint8_t xpos[2]{JOY_DEFAULT_POS, JOY_DEFAULT_POS};
  uint8_t ypos[2]{JOY_DEFAULT_POS, JOY_DEFAULT_POS};
  bool joybutton[3]{false, false, false};
  uint64_t buttonlatch[3]{0, 0, 0};
  int16_t trim_x = 0;
  int16_t trim_y = 0;

  JoystickConfig_t config{};
  bool joyquitevent = false;

  HostInterface_t* host = nullptr;
  int slot = 0;

  JoystickPeripheral_t() = default;
};

static auto Joy_IO_ReadButton(void* instance, uint16_t pc, uint16_t addr,
                              uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t {
  (void)pc;
  (void)bWrite;
  (void)d;
  (void)nCyclesLeft;
  if (instance == nullptr) return MemReadFloatingBus(nCyclesLeft);
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);

  auto res =
      static_cast<uint8_t>(MemReadFloatingBus(nCyclesLeft) & JOY_BIT_MASK);
  int button = static_cast<int>(addr - ADDR_BUTTON0);
  if (button >= 0 && button < 3) {
    if (jp->joybutton[button] || (jp->buttonlatch[button] > 0)) {
      res |= JOY_HIGH_BIT;
    }
  }
  return res;
}

static auto Joy_IO_ReadPosition(void* instance, uint16_t pc, uint16_t addr,
                                uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft)
    -> uint8_t {
  (void)pc;
  (void)bWrite;
  (void)d;
  if (instance == nullptr) return MemReadFloatingBus(nCyclesLeft);
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);

  auto res =
      static_cast<uint8_t>(MemReadFloatingBus(nCyclesLeft) & JOY_BIT_MASK);
  int pdl = addr & 0x03;

  uint32_t val = (pdl & 1) ? jp->ypos[pdl >> 1] : jp->xpos[pdl >> 1];

  // Apple II analog timing: high bit is set if (cycles - reset_cycle) <
  // constant * position
  uint64_t elapsed = g_nCumulativeCycles - jp->reset_cycle;
  uint64_t limit = static_cast<uint64_t>(val) * PADDLE_TIMING_MULTIPLIER +
                   PADDLE_TIMING_OFFSET;

  if (elapsed < limit) {
    res |= JOY_HIGH_BIT;
  }
  return res;
}

static auto Joy_IO_ResetPosition(void* instance, uint16_t pc, uint16_t addr,
                                 uint8_t bWrite, uint8_t d,
                                 uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)bWrite;
  (void)d;
  if (instance == nullptr) return MemReadFloatingBus(cycles_left);
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);

  CpuCalcCycles(cycles_left);
  jp->reset_cycle = g_nCumulativeCycles;
  return MemReadFloatingBus(cycles_left);
}

static auto Joystick_ABI_Init(int slot, HostInterface_t* host) -> void* {
  auto* jp = new JoystickPeripheral_t{};
  jp->host = host;
  jp->slot = slot;

  // $C061-$C063: Buttons
  for (uint16_t addr = ADDR_BUTTON0; addr <= ADDR_BUTTON2; ++addr) {
    host->RegisterDirectIO(jp, addr, Joy_IO_ReadButton, nullptr);
  }

  // $C064-$C067: Paddles
  for (uint16_t addr = ADDR_PADDLE0; addr <= ADDR_PADDLE3; ++addr) {
    host->RegisterDirectIO(jp, addr, Joy_IO_ReadPosition, nullptr);
  }

  // $C070: Paddle Reset
  host->RegisterDirectIO(jp, ADDR_PADDLE_RESET, Joy_IO_ResetPosition,
                         Joy_IO_ResetPosition);

  return jp;
}

static void Joystick_ABI_Reset(void* instance) {
  if (instance == nullptr) {
    return;
  }
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);
  for (auto& latch : jp->buttonlatch) {
    latch = 0;
  }
  for (auto& button : jp->joybutton) {
    button = false;
  }
  for (auto& x : jp->xpos) {
    x = JOY_DEFAULT_POS;
  }
  for (auto& y : jp->ypos) {
    y = JOY_DEFAULT_POS;
  }
  jp->reset_cycle = 0;
}

static void Joystick_ABI_Shutdown(void* instance) {
  if (instance == nullptr) {
    return;
  }
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);
  delete jp;
}

static void Joystick_ABI_Think(void* instance, uint32_t cycles) {
  if (instance == nullptr) return;
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);
  for (auto& latch : jp->buttonlatch) {
    if (latch > cycles) {
      latch -= cycles;
    } else {
      latch = 0;
    }
  }
}

static auto Joystick_ABI_Command(void* instance, uint32_t cmd, const void* data,
                                 size_t size) -> PeripheralStatus {
  if (instance == nullptr) return PERIPHERAL_ERROR;
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);

  switch (static_cast<JoystickCmd_e>(cmd)) {
    case JOY_CMD_SET_AXIS: {
      if (size < sizeof(JoystickAxisPayload_t)) return PERIPHERAL_ERROR;
      const auto* payload = static_cast<const JoystickAxisPayload_t*>(data);
      if (payload->joystick < 2) {
        if (payload->axis == 0) {
          jp->xpos[payload->joystick] = payload->value;
        } else {
          jp->ypos[payload->joystick] = payload->value;
        }
      }
      return PERIPHERAL_OK;
    }
    case JOY_CMD_SET_BUTTON: {
      if (size < sizeof(JoystickButtonPayload_t)) return PERIPHERAL_ERROR;
      const auto* payload = static_cast<const JoystickButtonPayload_t*>(data);
      if (payload->button < 3) {
        if (payload->down && !jp->joybutton[payload->button]) {
          jp->buttonlatch[payload->button] = GetButtonTime();
        }
        jp->joybutton[payload->button] = payload->down;
      }
      return PERIPHERAL_OK;
    }
    case JOY_CMD_SET_TRIM: {
      if (size < sizeof(JoystickTrimPayload_t)) return PERIPHERAL_ERROR;
      const auto* payload = static_cast<const JoystickTrimPayload_t*>(data);
      if (payload->axis_x) {
        jp->trim_x = payload->value;
      } else {
        jp->trim_y = payload->value;
      }
      return PERIPHERAL_OK;
    }
    case JOY_CMD_RESET: {
      Joystick_ABI_Reset(instance);
      return PERIPHERAL_OK;
    }
    case JOY_CMD_SET_CONFIG: {
      if (size < sizeof(JoystickConfig_t)) return PERIPHERAL_ERROR;
      memcpy(&jp->config, data, sizeof(JoystickConfig_t));
      return PERIPHERAL_OK;
    }
    default:
      break;
  }
  return PERIPHERAL_ERROR;
}

static auto Joystick_ABI_Query(void* instance, uint32_t cmd, void* out,
                               size_t* size) -> PeripheralStatus {
  if (!instance || !out || !size) return PERIPHERAL_ERROR;
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);

  switch (static_cast<JoystickQuery_e>(cmd)) {
    case JOY_QUERY_CONFIG: {
      if (*size < sizeof(JoystickConfig_t)) return PERIPHERAL_ERROR;
      memcpy(out, &jp->config, sizeof(JoystickConfig_t));
      *size = sizeof(JoystickConfig_t);
      return PERIPHERAL_OK;
    }
    case JOY_QUERY_EXIT_EVENT: {
      if (*size < 1) return PERIPHERAL_ERROR;
      *static_cast<uint8_t*>(out) = jp->joyquitevent ? 1 : 0;
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
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);

  if (buffer == nullptr) {
    *size = sizeof(SS_IO_Joystick);
    return PERIPHERAL_OK;
  }

  if (*size < sizeof(SS_IO_Joystick)) {
    return PERIPHERAL_ERROR;
  }

  auto* ss = static_cast<SS_IO_Joystick*>(buffer);
  ss->g_nJoyCntrResetCycle = jp->reset_cycle;

  *size = sizeof(SS_IO_Joystick);
  return PERIPHERAL_OK;
}

static auto Joystick_ABI_LoadState(void* instance, const void* buffer,
                                   size_t size) -> PeripheralStatus {
  if (instance == nullptr || buffer == nullptr || size < sizeof(SS_IO_Joystick)) {
    return PERIPHERAL_ERROR;
  }
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);
  const auto* ss = static_cast<const SS_IO_Joystick*>(buffer);

  jp->reset_cycle = ss->g_nJoyCntrResetCycle;

  return PERIPHERAL_OK;
}

Peripheral_t g_joystick_peripheral = {
    .abi_version      = LINAPPLE_ABI_VERSION,
    .id               = "linapple.joystick",
    .name             = "Joystick",
    .description      = "Analog joystick and paddle emulation",
    .author           = "LinApple Contributors",
    .version          = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_INTERNAL,
    .default_slot     = 0,
    .init             = Joystick_ABI_Init,
    .reset            = Joystick_ABI_Reset,
    .shutdown         = Joystick_ABI_Shutdown,
    .think            = Joystick_ABI_Think,
    .on_vblank        = nullptr,
    .save_state       = Joystick_ABI_SaveState,
    .load_state       = Joystick_ABI_LoadState,
    .command          = Joystick_ABI_Command,
    .query            = Joystick_ABI_Query
};

PERIPHERAL_REGISTER(g_joystick_peripheral)
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables)
