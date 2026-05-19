// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
//             modernize-use-trailing-return-type)
// Justification: This header defines the C99-compatible public ABI for the
// Mouse subsystem. C system headers, typedefs, and C-style return types
// are required for cross-language compatibility with C-based consumers.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  mouse_cmd_set_pos = 0,    // data: MousePosPayload_t
  mouse_cmd_set_button = 1  // data: MouseButtonPayload_t
} MouseCmd_e;

typedef enum {
  mouse_query_is_active = 0x0001  // out: uint8_t (0=inactive, 1=active)
} MouseQuery_e;

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

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
//           modernize-use-trailing-return-type)
