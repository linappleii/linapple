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

// 0x0002-0x0004 stay unassigned: senders built against older headers emit
// them. 0x0001 (a switch level), 0x0005 (the shift-key mod jumper) and 0x0006
// (the connector's pull-down mask) are retired and never reused: the three
// pushbutton inputs are the motherboard's, which composes them from the
// keyboard, the connector and the pull-downs itself.
typedef enum {
  JOYSTICK_CMD_SET_AXIS = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0000
} JoystickCommand_t;

typedef struct {
  uint8_t joystick;
  uint8_t axis;
  uint8_t value;
  uint8_t padding;
} JoystickAxisPayload_t;

// The positions and trim are the player's hands and the host's calibration,
// and the switch levels the motherboard's, not the port's state: x_pos, y_pos,
// buttons, trim_x, trim_y and the reserved bytes are written as zeros and read
// past, and keep their place so every frame ever written loads. A
// trigger_cycle of 0 is a timer never triggered.
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
