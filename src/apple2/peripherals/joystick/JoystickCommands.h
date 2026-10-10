// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-use-using)
// Justification: This header defines the C99-compatible public ABI for the
// game I/O port.

#include <stdint.h>

#include "apple2/peripherals/Peripheral_Subsystems.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { JOYSTICK_STATE_VERSION = 1 };

// 0x0001, 0x0005 and 0x0006 are retired, never reused: the pushbutton inputs
// are the motherboard's now. 0x0002-0x0004 stay unassigned for senders built
// against older headers.
typedef enum {
  JOYSTICK_CMD_SET_AXIS = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0000,
} JoystickCommand;

typedef struct {
  uint8_t joystick;
  uint8_t axis;
  uint8_t value;
  uint8_t padding;
} JoystickAxisPayload;

// x_pos, y_pos, buttons, trim_x, trim_y and the reserved bytes are the host's
// or the motherboard's, written as zeros and read past; they keep their place
// so every frame ever written loads. A trigger_cycle of 0 is never triggered.
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
} JoystickSaveState;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-use-using)
