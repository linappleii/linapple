// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, bugprone-easily-swappable-parameters)
// Justification: This header defines C-compatible structures and types for the
// keyboard translation system to ensure interoperability across different
// frontends. Public ABI functions have fixed parameter types.

#include <stdint.h>

#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  KBD_MODE_SYMBOLIC = 0,
  KBD_MODE_POSITIONAL = 1
} KeyboardMappingMode_t;

typedef enum {
  QUICKSAVE_MODE_ALT = 0,
  QUICKSAVE_MODE_CTRL = 1,
  QUICKSAVE_MODE_ALT_CTRL = 2,
  QUICKSAVE_MODE_DISABLED = 3
} QuickSaveMode_t;

LinAppleKey keyboard_symbolic_to_core(int key, uint32_t mod);
LinAppleKey keyboard_scancode_to_positional(uint32_t scancode);

uint32_t keyboard_parse_host_key(const char* name);
uint8_t keyboard_parse_apple2_val(const char* name, uint8_t* out_flags);
void keyboard_apply_custom_mappings();
bool keyboard_has_custom_mappings();

QuickSaveMode_t keyboard_get_quicksave_mode(void);
void keyboard_set_quicksave_mode(QuickSaveMode_t mode);
bool keyboard_is_quicksave_combo(uint32_t sym, uint32_t mod, int* out_slot,
                                 bool* out_is_save);
bool keyboard_get_hotkeys_enabled(void);
void keyboard_set_hotkeys_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, bugprone-easily-swappable-parameters)
