// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access, bugprone-easily-swappable-parameters)
// Justification: This file implements the platform-agnostic keyboard translation
// layer, mapping host scancodes and keycodes to LinApple internal keys.
// Functions in this file follow a C99-compatible ABI where parameter types are
// fixed for interoperability.

#include "frontends/common/KeyboardTranslator.h"

#include <cstdint>

#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

namespace keyboard_translator {

static constexpr int ascii_printable_min = 32;   // ' '
static constexpr int ascii_printable_max = 126;  // '~'

// We currently assume SDL scancodes for all positional mapping.
static constexpr uint32_t sdl_scancode_min = 4;   // "A" key
static constexpr uint32_t sdl_scancode_max = 82;  // "Up Arrow" key

static constexpr int ascii_cr = 0x0D;
static constexpr int ascii_esc = 0x1B;
static constexpr int ascii_bs = 0x08;
static constexpr int ascii_tab = 0x09;
static constexpr int ascii_del = 0x7F;

static constexpr uint32_t positional_key_base = 0x500;

}  // namespace keyboard_translator

auto keyboard_symbolic_to_core(int key, uint32_t mod) -> LinAppleKey {
  (void)mod;

  namespace kt = keyboard_translator;

  if (key >= kt::ascii_printable_min && key <= kt::ascii_printable_max) {
    return static_cast<LinAppleKey>(key);
  }

  switch (key) {
    case kt::ascii_cr:
      return LINAPPLE_KEY_RETURN;
    case kt::ascii_esc:
      return LINAPPLE_KEY_ESCAPE;
    case kt::ascii_bs:
      return LINAPPLE_KEY_BACKSPACE;
    case kt::ascii_tab:
      return LINAPPLE_KEY_TAB;
    case kt::ascii_del:
      return LINAPPLE_KEY_DELETE;
    default:
      return LINAPPLE_KEY_UNKNOWN;
  }
}

auto keyboard_scancode_to_positional(uint32_t scancode) -> LinAppleKey {
  namespace kt = keyboard_translator;

  // SDL Scancodes map directly to our LINAPPLE_KEY_POS_* values
  // if we align the enum values correctly (which we did in LinAppleCore.h).
  if (scancode >= kt::sdl_scancode_min && scancode <= kt::sdl_scancode_max) {
    return static_cast<LinAppleKey>(kt::positional_key_base + scancode);
  }
  return LINAPPLE_KEY_UNKNOWN;
}

// NOLINTEND(cppcoreguidelines-pro-type-union-access, bugprone-easily-swappable-parameters)
