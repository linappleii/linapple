/*
 * MouseCommands.h - Mouse Peripheral Command ABI
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  MOUSE_CMD_SET_POS = 0,    // data: MousePosPayload_t
  MOUSE_CMD_SET_BUTTON = 1  // data: MouseButtonPayload_t
} MouseCmd_e;

typedef struct {
  int x;
  int x_range;
  int y;
  int y_range;
} MousePosPayload_t;

typedef struct {
  uint8_t button;  // 0 or 1
  bool down;
} MouseButtonPayload_t;

#ifdef __cplusplus
}
#endif
