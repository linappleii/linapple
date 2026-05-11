/*
 * JoystickCommands.h - Joystick Peripheral Command ABI
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JOY_CMD_SET_AXIS = 0,    // data: JoystickAxisPayload_t
  JOY_CMD_SET_BUTTON = 1,  // data: JoystickButtonPayload_t
  JOY_CMD_SET_TRIM = 2,    // data: JoystickTrimPayload_t
  JOY_CMD_RESET = 3,       // data: none
  JOY_CMD_SET_CONFIG = 4   // data: JoystickConfig_t
} JoystickCmd_e;

typedef enum {
  JOY_QUERY_CONFIG = 0x0001,    // out: JoystickConfig_t
  JOY_QUERY_EXIT_EVENT = 0x0002 // out: uint8_t (0 or 1)
} JoystickQuery_e;

typedef struct {
  uint32_t joytype[2];
  uint32_t joyindex[2];
  uint32_t joybutton[2]; // joy1button1, joy1button2 (for joy 0)
  uint32_t joy2button1;
  uint32_t joyaxis[2][2]; // [joy][axis]
  uint32_t joyexitenable;
  uint32_t joyexitbutton[2];
} JoystickConfig_t;

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
