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
  keyb_cmd_event = 0x0001,
  keyb_cmd_set_caps = 0x0002,
  keyb_cmd_set_rocker = 0x0003,
  keyb_cmd_set_mods = 0x0004
} KeyboardCmd_e;

typedef enum {
  keyb_query_mods = 0x0001,
  keyb_query_rocker = 0x0002
} KeyboardQuery_e;

typedef struct {
  uint8_t shift;
  uint8_t ctrl;
  uint8_t alt;
  uint8_t gui;
  uint8_t caps;
} KeyboardModifiers_t;

typedef struct {
  uint8_t ascii;
  uint8_t is_down;
} KeyboardEvent_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
//           cppcoreguidelines-use-enum-class)
