// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
//             cppcoreguidelines-use-enum-class)
// Justification: This header defines a C-compatible binary interface for the
// keyboard command and query system, requiring C-style headers, structs, and
// enums.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  keyboard_layout_us = 0,
  keyboard_layout_uk = 1,
  keyboard_layout_fr = 2,
  keyboard_layout_de = 3,
  keyboard_layout_es = 4,
  keyboard_layout_it = 5,
  keyboard_layout_se = 6,
  keyboard_layout_dk = 7,
  keyboard_layout_ch = 8,
  keyboard_layout_ca = 9,
  keyboard_layout_jp_roman = 10,
  keyboard_layout_jp_kana = 11
} KeyboardLayout_t;

typedef enum {
  keyboard_cmd_event = 0x0001,       /**< data: KeyboardEvent_t */
  keyboard_cmd_set_caps = 0x0002,    /**< data: uint8_t (0=off, 1=on) */
  keyboard_cmd_set_rocker = 0x0003,  /**< data: uint8_t (0=off, 1=on) */
  keyboard_cmd_set_mods = 0x0004,    /**< data: KeyboardModifiers_t */
  keyboard_cmd_set_layout = 0x0005   /**< data: uint8_t (KeyboardLayout_t) */
} KeyboardCmd_t;

typedef enum {
  keyboard_query_mods = 0x0001,      /**< out: KeyboardModifiers_t */
  keyboard_query_rocker = 0x0002     /**< out: uint8_t (0=off, 1=on) */
} KeyboardQuery_t;

typedef struct {
  uint8_t shift;
  uint8_t ctrl;
  uint8_t alt;
  uint8_t gui;
  uint8_t caps;
  uint8_t reserved[3];               /**< Padding for 8-byte ABI alignment */
} KeyboardModifiers_t;

typedef struct {
  uint32_t key;
  uint8_t is_down;
  uint8_t mod_shift;
  uint8_t mod_ctrl;
  uint8_t mod_alt;
  uint8_t mod_gui;
  uint8_t reserved[3];               /**< Padding for 12-byte ABI alignment */
} KeyboardEvent_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
//           cppcoreguidelines-use-enum-class)
