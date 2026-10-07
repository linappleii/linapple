// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification:
// This header defines a language-neutral C ABI. C system headers, typedefs, and
// C-style arrays are required for compatibility with C-based consumers.

#include <stdint.h>

#include "apple2/peripherals/Peripheral_Subsystems.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { KEYBOARD_STATE_VERSION = 1, KEYBOARD_MAP_SIZE = 128 };

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

/* The keyboard hands the motherboard a seven-bit code and a strobe (Apple II
 * Reference Manual 1979, p. 6; Apple IIe Technical Reference Manual, p. 13),
 * so that is what keyboard_cmd_key carries. Which host key means which code
 * is the host's business. Indices 0x0001 to 0x0008 carried host keys and host
 * configuration for the card to translate; they are superseded by
 * keyboard_cmd_key and go with their last senders. */
typedef enum {
  keyboard_cmd_event =
      PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x0001, /**< data: KeyboardEvent_t */
  keyboard_cmd_set_caps = PERIPHERAL_SUBSYSTEM_KEYBOARD |
                          0x0002, /**< data: uint8_t (0=off, 1=on) */
  keyboard_cmd_set_rocker = PERIPHERAL_SUBSYSTEM_KEYBOARD |
                            0x0003, /**< data: uint8_t (0=off, 1=on) */
  keyboard_cmd_set_mods =
      PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x0004, /**< data: KeyboardModifiers_t */
  keyboard_cmd_set_layout = PERIPHERAL_SUBSYSTEM_KEYBOARD |
                            0x0005, /**< data: uint8_t (KeyboardLayout_t) */
  keyboard_cmd_set_custom_key = PERIPHERAL_SUBSYSTEM_KEYBOARD |
                                0x0006, /**< data: KeyboardCustomKeyPayload_t */
  keyboard_cmd_clear_custom_keys =
      PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x0007, /**< data: none */
  keyboard_cmd_set_auto_repeat =
      PERIPHERAL_SUBSYSTEM_KEYBOARD |
      0x0008, /**< data: uint8_t (0=disabled, 1=enabled) */
  keyboard_cmd_key =
      PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x0009, /**< data: KeyboardKeyEvent_t */
  /* The host's hands are off every key, as on focus loss. data: none */
  keyboard_cmd_release_all = PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x000A,
  /* The REPT key of a II or II Plus keyboard. data: uint8_t (0=up, 1=down) */
  keyboard_cmd_rept = PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x000B
} KeyboardCmd_t;

typedef enum {
  keyboard_query_mods =
      PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x0001, /**< out: KeyboardModifiers_t */
  keyboard_query_rocker =
      PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x0002 /**< out: uint8_t (0=off, 1=on) */
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

/* host_key is the host's own identity for the key, so that a release pairs
 * with its press whatever the modifiers did in between; the card never
 * interprets it. apple_code is the seven-bit code the keyboard would put on
 * the bus, 0..127; a value above is refused. */
typedef struct {
  uint32_t host_key;
  uint8_t apple_code;
  uint8_t is_down;
  uint8_t reserved[6];
} KeyboardKeyEvent_t;

typedef struct {
  uint32_t scancode;
  uint8_t normal_val;
  uint8_t shift_val;
  uint8_t ctrl_val;
  /* 1 = active override. The flag values 2 (Open Apple) and 4 (Solid Apple)
   * are stored and saved by the card and interpreted by the host, which drives
   * the game port's switch lines for a flagged key instead of sending its key
   * event. */
  uint8_t flags;
} KeyboardCustomKeyPayload_t;

typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint32_t keys_down_count;
  uint32_t repeat_key;
  uint32_t repeat_scancode;
  uint32_t repeat_delay_cycles;
  uint8_t current_latch;
  uint8_t strobe;
  uint8_t rocker_switch;
  uint8_t shift_key;
  uint8_t ctrl_key;
  uint8_t open_apple;
  uint8_t solid_apple;
  uint8_t caps_lock;
  uint8_t alternate_layout;
  uint8_t repeating;
  uint8_t has_custom_keys;
  uint8_t auto_repeat_enabled;
  uint8_t custom_map[KEYBOARD_MAP_SIZE];
  uint8_t custom_shift_map[KEYBOARD_MAP_SIZE];
  uint8_t custom_ctrl_map[KEYBOARD_MAP_SIZE];
  uint8_t custom_flags[KEYBOARD_MAP_SIZE];
  uint8_t reserved[4];
} KeyboardSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
