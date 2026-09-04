// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)

typedef enum {
  peripheral_ok = 0,
  peripheral_error = -1,
  peripheral_incompatible = -2,
  peripheral_busy = -3
} PeripheralStatus_t;

typedef enum {
  log_debug = 0,
  log_info,
  log_warn,
  log_error
} PeripheralLogLevel_t;

enum IrqSrc_t {
  is_6522 = 0,
  is_speech,
  is_ssc,
  is_mouse,
  is_slot1,
  is_slot2,
  is_slot3,
  is_slot4,
  is_slot5,
  is_slot6,
  is_slot7
};

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
  keyboard_cmd_event = 0x0001,          /**< data: KeyboardEvent_t */
  keyboard_cmd_set_caps = 0x0002,       /**< data: uint8_t (0=off, 1=on) */
  keyboard_cmd_set_rocker = 0x0003,     /**< data: uint8_t (0=off, 1=on) */
  keyboard_cmd_set_mods = 0x0004,       /**< data: KeyboardModifiers_t */
  keyboard_cmd_set_layout = 0x0005,     /**< data: uint8_t (KeyboardLayout_t) */
  keyboard_cmd_set_custom_key = 0x0006, /**< data: KeyboardCustomKeyPayload_t */
  keyboard_cmd_clear_custom_keys = 0x0007 /**< data: none */
} KeyboardCmd_t;

typedef enum {
  keyboard_query_mods = 0x0001,  /**< out: KeyboardModifiers_t */
  keyboard_query_rocker = 0x0002 /**< out: uint8_t (0=off, 1=on) */
} KeyboardQuery_t;

typedef struct {
  uint8_t shift;
  uint8_t ctrl;
  uint8_t alt;
  uint8_t gui;
  uint8_t caps;
  uint8_t reserved[3]; /**< Padding for 8-byte ABI alignment */
} KeyboardModifiers_t;

typedef struct {
  uint32_t key;
  uint8_t is_down;
  uint8_t mod_shift;
  uint8_t mod_ctrl;
  uint8_t mod_alt;
  uint8_t mod_gui;
  uint8_t reserved[3]; /**< Padding for 12-byte ABI alignment */
} KeyboardEvent_t;

typedef struct {
  uint32_t scancode;
  uint8_t normal_val;
  uint8_t shift_val;
  uint8_t ctrl_val;
  uint8_t flags; /**< 1 = active override, 2 = open_apple, 4 = closed_apple */
} KeyboardCustomKeyPayload_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)
