// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
//             cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification: This header defines a language-neutral C ABI. C system
// headers, typedefs, and C-style arrays are required for compatibility with
// C-based consumers.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JOY_CMD_SET_AXIS = 0,
  JOY_CMD_SET_BUTTON = 1,
  JOY_CMD_SET_TRIM = 2,
  JOY_CMD_RESET = 3,
  JOY_CMD_SET_CONFIG = 4
} JoystickCmd_e;

typedef enum {
  JOY_QUERY_CONFIG = 0x0001,
  JOY_QUERY_EXIT_EVENT = 0x0002
} JoystickQuery_e;

typedef struct {
  uint32_t joy_type[2];
  uint32_t joy_index[2];
  uint32_t joy0_button_map[2];
  uint32_t joy1_button_map;
  uint32_t joy_axis[2][2];
  uint32_t joy_exit_enable;
  uint32_t joy_exit_button[2];
} JoystickConfig_t;

typedef struct {
  uint8_t joystick;
  uint8_t axis;
  uint8_t value;
} JoystickAxisPayload_t;

typedef struct {
  uint8_t button;
  bool down;
} JoystickButtonPayload_t;

typedef struct {
  bool axis_x;
  int16_t value;
} JoystickTrimPayload_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
//           cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
