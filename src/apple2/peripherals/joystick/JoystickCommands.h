// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification: This header defines the C99-compatible public ABI for the
// game I/O port.

#include <stdint.h>

#include "apple2/peripherals/Peripheral_Subsystems.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { JOYSTICK_STATE_VERSION = 1 };

// 0x0002-0x0004 stay unassigned: senders built against older headers emit them.
typedef enum {
  JOYSTICK_CMD_SET_AXIS = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0000,
  JOYSTICK_CMD_SET_BUTTON = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0001,
  JOYSTICK_CMD_SET_SHIFT_KEY_MOD = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0005,
  JOYSTICK_CMD_SET_PULLDOWNS = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0006
} JoystickCommand_t;

typedef struct {
  uint8_t joystick;
  uint8_t axis;
  uint8_t value;
  uint8_t padding;
} JoystickAxisPayload_t;

// source 0 is the game connector's switch, 1 the keyboard's wired in parallel.
typedef struct {
  uint8_t button;
  uint8_t down;
  uint8_t source;
  uint8_t padding;
} JoystickButtonPayload_t;

// The positions, switch levels and trim are the player's hands and the host's
// calibration, not the port's state: x_pos, y_pos, buttons, trim_x, trim_y
// and the reserved bytes are written as zeros and read past, and keep their
// place so every frame ever written loads. A trigger_cycle of 0 is a timer
// never triggered.
typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint64_t trigger_cycle[4];
  uint8_t x_pos[2];
  uint8_t y_pos[2];
  uint8_t buttons[3];
  uint8_t reserved0;
  int16_t trim_x;
  int16_t trim_y;
  uint8_t reserved1[4];
} JoystickSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
