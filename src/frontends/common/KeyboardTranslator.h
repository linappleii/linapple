// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "core/LinAppleCore.h"

enum KeyboardMappingMode : uint8_t {
  kbd_mode_symbolic = 0,
  kbd_mode_positional = 1,
};

enum QuickSaveMode : uint8_t {
  quicksave_mode_alt = 0,
  quicksave_mode_ctrl = 1,
  quicksave_mode_alt_ctrl = 2,
  quicksave_mode_disabled = 3,
};

// scancode is the USB HID usage the maps index (SDL's scancode); 0 says the
// host has none, as a terminal does, and such a key is read symbolically.
// keycode is printable ASCII or a LinAppleKey value.
struct KeyboardHostKey {
  uint32_t scancode;
  uint32_t keycode;
  bool shift;
  bool ctrl;
};

// A [Keyboard.Custom] entry that is a switch rather than a character.
enum KeyboardCustomSwitch : uint8_t {
  keyboard_custom_switch_none = 0,
  keyboard_custom_switch_open_apple,
  keyboard_custom_switch_solid_apple,
  keyboard_custom_switch_rept,
};

// False for a key with no Apple meaning, which must not reach the card. The
// custom table is consulted first, then the layout by Mapping Mode.
auto keyboard_translate(const KeyboardHostKey* key, uint8_t* apple_code)
    -> bool;
auto keyboard_custom_switch(uint32_t scancode) -> KeyboardCustomSwitch;

auto keyboard_symbolic_to_core(int key, uint32_t mod) -> LinAppleKey;

// CAPS LOCK selects the upper-case half of the keyboard ROM (Apple IIe
// Technical Reference Manual, Table 2-3), a translation input and not
// bus-visible state. It starts down: Applesoft takes no lower-case keywords.
auto keyboard_set_caps(bool on) -> void;
auto keyboard_get_caps() -> bool;
auto keyboard_get_caps_mode() -> int;
auto keyboard_set_caps_mode(int mode) -> void;
auto keyboard_set_mapping_mode(KeyboardMappingMode mode) -> void;
auto keyboard_get_mapping_mode() -> KeyboardMappingMode;
// Read in positional mode while the rocker is on; a blank entry falls back to
// the US table.
auto keyboard_set_layout(uint8_t layout) -> void;
auto keyboard_get_layout() -> uint8_t;

auto keyboard_parse_host_key(const char* name) -> uint32_t;
auto keyboard_parse_apple2_val(const char* name, uint8_t* out_flags) -> uint8_t;
auto keyboard_apply_custom_mappings() -> void;
auto keyboard_has_custom_mappings() -> bool;
auto frontend_update_keyboard_mapping() -> void;

auto keyboard_get_quicksave_mode() -> QuickSaveMode;
auto keyboard_set_quicksave_mode(QuickSaveMode mode) -> void;
auto keyboard_is_quicksave_combo(uint32_t sym, uint32_t mod, int* out_slot,
                                 bool* out_is_save) -> bool;
auto keyboard_get_hotkeys_enabled() -> bool;
auto keyboard_set_hotkeys_enabled(bool enabled) -> void;
