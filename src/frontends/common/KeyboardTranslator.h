// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
// cppcoreguidelines-use-enum-class, bugprone-easily-swappable-parameters)
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

LinAppleKey keyboard_symbolic_to_core(int key, uint32_t mod);
LinAppleKey keyboard_scancode_to_positional(uint32_t scancode);

uint32_t keyboard_parse_host_key(const char* name);
uint8_t keyboard_parse_apple2_val(const char* name, uint8_t* out_flags);
void keyboard_apply_custom_mappings();
bool keyboard_has_custom_mappings();

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
// cppcoreguidelines-use-enum-class, bugprone-easily-swappable-parameters)
