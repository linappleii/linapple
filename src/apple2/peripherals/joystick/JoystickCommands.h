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

// Indices 0x0002 to 0x0004 once named a trim, a reset and a host input
// configuration; they are retired and never reused, so a sender built against
// that header is answered peripheral_incompatible rather than misread.
typedef enum {
  JOYSTICK_CMD_SET_AXIS = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0000,
  JOYSTICK_CMD_SET_BUTTON = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0001,
  JOYSTICK_CMD_SET_SHIFT_KEY_MOD = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0005,
  JOYSTICK_CMD_SET_PULLDOWNS = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0006
} JoystickCommand_t;

// joystick 0 or 1, axis 0 (x) or 1 (y); paddle 2j + axis of joystick j.
typedef struct {
  uint8_t joystick;
  uint8_t axis;
  uint8_t value;
  uint8_t padding;
} JoystickAxisPayload_t;

// One of the three pushbutton lines, PB0 to PB2, and the level a source
// drives it to: source 0 is the switch on the game connector, source 1 the
// switch the //e wires in parallel with it on the keyboard (Open Apple on
// PB0, Solid Apple on PB1, the shift key through the shift-key mod on PB2;
// Apple IIe Technical Reference Manual, pp. 13 and 41).
typedef struct {
  uint8_t button;
  uint8_t down;
  uint8_t source;
  uint8_t padding;
} JoystickButtonPayload_t;

// The version-1 frame. trigger_cycle holds the cumulative cycle at which each
// of the four NE558 timers was last triggered, 0 for a timer never triggered
// since power-on, and is the port's whole state: the positions and switch
// levels are the player's hands, which the host sends again, and trim is host
// calibration. x_pos, y_pos, buttons, trim_x, trim_y and the reserved bytes
// are written as zeros and read past; they keep their place so every frame
// ever written loads.
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
