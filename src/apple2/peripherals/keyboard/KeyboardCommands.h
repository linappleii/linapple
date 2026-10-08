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

/* The keyboard hands the motherboard a seven-bit code and a strobe (Apple II
 * Reference Manual 1979, p. 6; Apple IIe Technical Reference Manual, p. 13),
 * so that is what the card takes; translation is the host's. Command indices
 * 0x0001-0x0008 and query indices 0x0001-0x0002 once carried host keys and
 * are retired, never reused. */
typedef enum {
  keyboard_cmd_key = PERIPHERAL_SUBSYSTEM_KEYBOARD |
      0x0009, /**< data: KeyboardKeyEvent_t */
  /* The host's hands are off every key, as on focus loss. data: none */
  keyboard_cmd_release_all = PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x000A,
  /* The REPT key of a II or II Plus keyboard. data: uint8_t (0=up, 1=down) */
  keyboard_cmd_rept = PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x000B,
} KeyboardCmd_t;

/* host_key is the host's identity for the key, so a release pairs with its
 * press whatever the modifiers did in between; the card never interprets it.
 * An apple_code above 127 is refused. */
typedef struct {
  uint32_t host_key;
  uint8_t apple_code;
  uint8_t is_down;
  uint8_t reserved[6];
} KeyboardKeyEvent_t;

/* The 552-byte frame every .aws written so far carries. Only current_latch
 * and strobe are restored; the held keys, the repeat and the map, layout,
 * rocker and caps bytes are the player's hands or host configuration. */
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
