// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
// cppcoreguidelines-use-enum-class, bugprone-easily-swappable-parameters)
// Justification: This header defines C-compatible structures and types for the
// keyboard translation system to ensure interoperability across different
// frontends. Public ABI functions have fixed parameter types.

#include <stdint.h>

#include "core/LinAppleCore.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  KBD_MODE_SYMBOLIC = 0,
  KBD_MODE_POSITIONAL = 1
} KeyboardMappingMode_t;

LinAppleKey keyboard_symbolic_to_core(int key, uint32_t mod);
LinAppleKey keyboard_scancode_to_positional(uint32_t scancode);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
// cppcoreguidelines-use-enum-class, bugprone-easily-swappable-parameters)
