// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "core/LinAppleCore.h"

enum KeyboardMappingMode_t : uint8_t {
  KBD_MODE_SYMBOLIC = 0,
  KBD_MODE_POSITIONAL = 1
};

enum QuickSaveMode_t : uint8_t {
  QUICKSAVE_MODE_ALT = 0,
  QUICKSAVE_MODE_CTRL = 1,
  QUICKSAVE_MODE_ALT_CTRL = 2,
  QUICKSAVE_MODE_DISABLED = 3
};

auto keyboard_symbolic_to_core(int key, uint32_t mod) -> LinAppleKey_t;
auto keyboard_scancode_to_positional(uint32_t scancode) -> LinAppleKey_t;

auto keyboard_parse_host_key(const char* name) -> uint32_t;
auto keyboard_parse_apple2_val(const char* name, uint8_t* out_flags) -> uint8_t;
auto keyboard_apply_custom_mappings() -> void;
auto keyboard_has_custom_mappings() -> bool;
// The game-port switch line a [Keyboard.Custom] entry turns its host key
// into: 0 for Open Apple, 1 for Solid Apple, -1 for a key that types.
auto keyboard_custom_apple_line(uint32_t scancode) -> int;

auto keyboard_get_quicksave_mode() -> QuickSaveMode_t;
auto keyboard_set_quicksave_mode(QuickSaveMode_t mode) -> void;
auto keyboard_is_quicksave_combo(uint32_t sym, uint32_t mod, int* out_slot,
                                 bool* out_is_save) -> bool;
auto keyboard_get_hotkeys_enabled() -> bool;
auto keyboard_set_hotkeys_enabled(bool enabled) -> void;
