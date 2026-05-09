/*
 * JoystickCommands.h - Joystick Peripheral Command ABI
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JOY_CMD_SET_AXIS = 0,    // data: JoystickAxisPayload_t
  JOY_CMD_SET_BUTTON = 1,  // data: JoystickButtonPayload_t
  JOY_CMD_SET_TRIM = 2     // data: JoystickTrimPayload_t
} JoystickCmd_e;

typedef struct {
  uint8_t joystick;  // 0 or 1
  uint8_t axis;      // 0 (X) or 1 (Y)
  uint8_t value;     // 0-255
} JoystickAxisPayload_t;

typedef struct {
  uint8_t button;  // 0, 1, or 2
  bool down;
} JoystickButtonPayload_t;

typedef struct {
  bool axis_x;
  int16_t value;
} JoystickTrimPayload_t;

#ifdef __cplusplus
}
#endif
