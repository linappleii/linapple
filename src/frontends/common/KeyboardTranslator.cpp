// SPDX-License-Identifier: GPL-2.0-only
// Justification: This file implements the platform-agnostic keyboard
// translation layer, mapping host scancodes and keycodes to LinApple internal
// keys. Functions in this file follow a C99-compatible ABI where parameter
// types are fixed for interoperability.
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access, bugprone-easily-swappable-parameters)

#include "frontends/common/KeyboardTranslator.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "apple2/peripherals/keyboard/Keyboard_Maps.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "core/Registry.h"

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

static auto trim_str(const std::string& str) -> std::string {
  size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, (last - first + 1));
}

static auto to_lower_str(std::string s) -> std::string {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

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

auto keyboard_parse_host_key(const char* name) -> uint32_t {
  if (name == nullptr) return keyb_idx_unknown;

  std::string s =
      keyboard_translator::to_lower_str(keyboard_translator::trim_str(name));
  if (s.empty()) return keyb_idx_unknown;

  if (s.length() == 1) {
    char c = s[0];
    if (c >= 'a' && c <= 'z') return keyb_idx_a + (c - 'a');
    if (c >= '1' && c <= '9') return keyb_idx_1 + (c - '1');
    if (c == '0') return keyb_idx_0;
    if (c == '-') return keyb_idx_minus;
    if (c == '=') return keyb_idx_equals;
    if (c == '[') return keyb_idx_leftbracket;
    if (c == ']') return keyb_idx_rightbracket;
    if (c == '\\') return keyb_idx_backslash;
    if (c == ';') return keyb_idx_semicolon;
    if (c == '\'') return keyb_idx_apostrophe;
    if (c == '`') return keyb_idx_grave;
    if (c == ',') return keyb_idx_comma;
    if (c == '.') return keyb_idx_period;
    if (c == '/') return keyb_idx_slash;
    if (c == ' ') return keyb_idx_space;
  }

  if (s == "return" || s == "enter") return keyb_idx_return;
  if (s == "escape" || s == "esc") return keyb_idx_escape;
  if (s == "backspace" || s == "bs") return keyb_idx_backspace;
  if (s == "tab") return keyb_idx_tab;
  if (s == "space" || s == "spacebar") return keyb_idx_space;
  if (s == "minus") return keyb_idx_minus;
  if (s == "equals" || s == "equal") return keyb_idx_equals;
  if (s == "leftbracket" || s == "bracketleft") return keyb_idx_leftbracket;
  if (s == "rightbracket" || s == "bracketright") return keyb_idx_rightbracket;
  if (s == "backslash") return keyb_idx_backslash;
  if (s == "semicolon") return keyb_idx_semicolon;
  if (s == "apostrophe" || s == "quote") return keyb_idx_apostrophe;
  if (s == "grave" || s == "backquote") return keyb_idx_grave;
  if (s == "comma") return keyb_idx_comma;
  if (s == "period" || s == "dot") return keyb_idx_period;
  if (s == "slash") return keyb_idx_slash;
  if (s == "caps" || s == "capslock" || s == "caps lock")
    return keyb_idx_capslock;

  if (s == "up" || s == "uparrow" || s == "up arrow") return keyb_idx_up;
  if (s == "down" || s == "downarrow" || s == "down arrow")
    return keyb_idx_down;
  if (s == "left" || s == "leftarrow" || s == "left arrow")
    return keyb_idx_left;
  if (s == "right" || s == "rightarrow" || s == "right arrow")
    return keyb_idx_right;

  if (s.length() >= 2 && s[0] == 'f') {
    try {
      int fnum = std::stoi(s.substr(1));
      if (fnum >= 1 && fnum <= 12) {
        return keyb_idx_f1 + (fnum - 1);
      }
    } catch (...) {
    }
  }

  return keyb_idx_unknown;
}

auto keyboard_parse_apple2_val(const char* name, uint8_t* out_flags)
    -> uint8_t {
  if (out_flags != nullptr) *out_flags = 0;
  if (name == nullptr) return 0;

  std::string s =
      keyboard_translator::to_lower_str(keyboard_translator::trim_str(name));
  if (s.empty()) return 0;

  if (s == "openapple" || s == "open apple" || s == "open_apple" || s == "oa") {
    if (out_flags != nullptr) *out_flags |= 2;
    return 0;
  }
  if (s == "closedapple" || s == "closed apple" || s == "closed_apple" ||
      s == "solidapple" || s == "solid apple" || s == "ca") {
    if (out_flags != nullptr) *out_flags |= 4;
    return 0;
  }

  if (s == "up" || s == "uparrow" || s == "up arrow") return 0x0B;
  if (s == "down" || s == "downarrow" || s == "down arrow") return 0x0A;
  if (s == "left" || s == "leftarrow" || s == "left arrow") return 0x08;
  if (s == "right" || s == "rightarrow" || s == "right arrow") return 0x15;
  if (s == "return" || s == "enter") return 0x0D;
  if (s == "escape" || s == "esc") return 0x1B;
  if (s == "backspace" || s == "bs") return 0x7F;
  if (s == "delete" || s == "del") return 0x7F;
  if (s == "tab") return 0x09;
  if (s == "space" || s == "spacebar") return 0x20;

  // Hex values like 0x0B or $15
  if ((s.length() > 2 && (s.rfind("0x", 0) == 0)) ||
      (s.length() > 1 && s[0] == '$')) {
    try {
      std::string hex_str = (s[0] == '$') ? s.substr(1) : s.substr(2);
      unsigned long val = std::stoul(hex_str, nullptr, 16);
      return static_cast<uint8_t>(val & 0xFF);
    } catch (...) {
    }
  }

  // Quoted character like 'a' or "a"
  if (s.length() >= 3 && ((s.front() == '\'' && s.back() == '\'') ||
                          (s.front() == '"' && s.back() == '"'))) {
    return static_cast<uint8_t>(s[1]);
  }

  // Single character
  if (s.length() == 1) {
    return static_cast<uint8_t>(s[0]);
  }

  // European accent translations
  if (s == "ä" || s == "é") return 0x7B;
  if (s == "ö" || s == "ù") return 0x7C;
  if (s == "ü" || s == "è") return 0x7D;
  if (s == "ä" || s == "°") return 0x5B;
  if (s == "ö" || s == "ç") return 0x5C;
  if (s == "ü" || s == "§") return 0x5D;
  if (s == "£") return 0x23;

  return 0;
}

void keyboard_apply_custom_mappings() {
  peripheral_command(0, keyboard_cmd_clear_custom_keys, nullptr, 0);

  const auto* custom_section =
      Configuration_t::instance().get_section("Keyboard.Custom");
  if (custom_section == nullptr || custom_section->empty()) {
    return;
  }

  for (const auto& entry : *custom_section) {
    uint32_t scancode = keyboard_parse_host_key(entry.first.c_str());
    if (scancode == keyb_idx_unknown || scancode >= keyb_map_size) {
      continue;
    }

    std::stringstream ss(entry.second);
    std::string token;
    std::vector<std::string> tokens;
    while (std::getline(ss, token, ',')) {
      tokens.push_back(keyboard_translator::trim_str(token));
    }

    if (tokens.empty()) continue;

    KeyboardCustomKeyPayload_t payload = {};
    payload.scancode = scancode;
    payload.flags = 1;  // Override active

    uint8_t flags0 = 0;
    payload.normal_val = keyboard_parse_apple2_val(tokens[0].c_str(), &flags0);
    payload.flags |= flags0;

    if (tokens.size() > 1) {
      uint8_t flags1 = 0;
      payload.shift_val = keyboard_parse_apple2_val(tokens[1].c_str(), &flags1);
      payload.flags |= flags1;
    } else {
      if (payload.normal_val >= 'a' && payload.normal_val <= 'z') {
        payload.shift_val = payload.normal_val - 'a' + 'A';
      } else {
        payload.shift_val = payload.normal_val;
      }
    }

    if (tokens.size() > 2) {
      uint8_t flags2 = 0;
      payload.ctrl_val = keyboard_parse_apple2_val(tokens[2].c_str(), &flags2);
      payload.flags |= flags2;
    } else {
      if (payload.normal_val != 0) {
        payload.ctrl_val = payload.normal_val & 0x1F;
      }
    }

    peripheral_command(0, keyboard_cmd_set_custom_key, &payload,
                       sizeof(payload));
  }
}

bool keyboard_has_custom_mappings() {
  const auto* custom_section =
      Configuration_t::instance().get_section("Keyboard.Custom");
  return (custom_section != nullptr && !custom_section->empty());
}

static QuickSaveMode_t g_quicksave_mode = QUICKSAVE_MODE_ALT;
static bool g_hotkeys_enabled = true;

QuickSaveMode_t keyboard_get_quicksave_mode(void) { return g_quicksave_mode; }

void keyboard_set_quicksave_mode(QuickSaveMode_t mode) {
  g_quicksave_mode = mode;
}

bool keyboard_get_hotkeys_enabled(void) { return g_hotkeys_enabled; }

void keyboard_set_hotkeys_enabled(bool enabled) { g_hotkeys_enabled = enabled; }

bool keyboard_is_quicksave_combo(uint32_t sym, uint32_t mod, int* out_slot,
                                 bool* out_is_save) {
  if (sym < '0' || sym > '9') {
    return false;
  }

  constexpr uint32_t kmod_shift = 0x0003;
  constexpr uint32_t kmod_ctrl = 0x00C0;
  constexpr uint32_t kmod_alt = 0x0300;

  const bool has_ctrl = (mod & kmod_ctrl) != 0;
  const bool has_alt = (mod & kmod_alt) != 0;
  const bool has_shift = (mod & kmod_shift) != 0;

  bool triggered = false;
  switch (g_quicksave_mode) {
    case QUICKSAVE_MODE_ALT:
      triggered = has_alt && !has_ctrl;
      break;
    case QUICKSAVE_MODE_CTRL:
      triggered = has_ctrl && !has_alt;
      break;
    case QUICKSAVE_MODE_ALT_CTRL:
      triggered = has_alt && has_ctrl;
      break;
    case QUICKSAVE_MODE_DISABLED:
      triggered = false;
      break;
  }

  if (triggered) {
    if (out_slot != nullptr) {
      *out_slot = static_cast<int>(sym - '0');
    }
    if (out_is_save != nullptr) {
      *out_is_save = has_shift;
    }
    return true;
  }
  return false;
}

// NOLINTEND(cppcoreguidelines-pro-type-union-access, bugprone-easily-swappable-parameters)
