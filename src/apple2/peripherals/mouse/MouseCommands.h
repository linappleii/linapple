// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification: This header defines the language-neutral C ABI for the Mouse
// peripheral. C system headers, typedefs, and fixed-size C-style arrays are
// required for compatibility with C-based consumers.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MOUSE_STATE_VERSION = 1, mouse_default_slot = 4 };

typedef enum {
  mouse_cmd_set_pos = 0,   /**< data: MousePosPayload_t */
  mouse_cmd_set_button = 1 /**< data: MouseButtonPayload_t */
} MouseCmd_t;

typedef enum {
  mouse_query_is_active = 0x0001 /**< out: uint8_t (0=inactive, 1=active) */
} MouseQuery_t;

typedef struct {
  int32_t x;
  int32_t x_range;
  int32_t y;
  int32_t y_range;
} MousePosPayload_t;

typedef struct {
  uint8_t button; /**< 0 or 1 */
  bool down;
  uint8_t padding[2];
} MouseButtonPayload_t;

typedef struct {
  // --- Header (8 bytes) ---
  uint32_t version;
  uint32_t struct_size;

  // --- Coordinates and Tracking (48 bytes) ---
  uint32_t internal_x;
  uint32_t internal_y;
  uint32_t min_x;
  uint32_t max_x;
  uint32_t min_y;
  uint32_t max_y;
  uint32_t range_x;
  uint32_t range_y;
  int32_t pos_x;
  int32_t pos_y;
  int32_t buffer_pos;
  int32_t data_len;

  // --- 6821 PIA State (18 bytes) ---
  uint8_t pia_ora;
  uint8_t pia_orb;
  uint8_t pia_ddra;
  uint8_t pia_ddrb;
  uint8_t pia_cra;
  uint8_t pia_crb;
  uint8_t pia_port_a_in;
  uint8_t pia_port_b_in;
  uint8_t pia_ca1_in;
  uint8_t pia_ca2_in;
  uint8_t pia_cb1_in;
  uint8_t pia_cb2_in;
  uint8_t pia_oca2;
  uint8_t pia_ocb2;
  uint8_t pia_irqa;
  uint8_t pia_irqb;
  uint8_t pia_port_a_shadow;
  uint8_t pia_port_b_shadow;

  // --- Runtime Operational State (15 bytes) ---
  uint8_t mode;
  uint8_t vblank_rising;
  uint8_t status_state;
  uint8_t btn0_prev;
  uint8_t btn1_prev;
  uint8_t buttons[2];
  uint8_t buffer[8];

  // --- Alignment Padding (3 bytes) ---
  uint8_t padding[3];
} MouseSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
