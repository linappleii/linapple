// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/KeyboardTranslator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/KeyboardMaps.h"

namespace keyboard_translator {

static constexpr int ascii_printable_min = 32;   // ' '
static constexpr int ascii_printable_max = 126;  // '~'

static constexpr int ascii_cr = 0x0D;
static constexpr int ascii_esc = 0x1B;
static constexpr int ascii_bs = 0x08;
static constexpr int ascii_tab = 0x09;
static constexpr int ascii_del = 0x7F;

static constexpr uint8_t apple_up = 0x0B;
static constexpr uint8_t apple_down = 0x0A;
static constexpr uint8_t apple_left = 0x08;
static constexpr uint8_t apple_right = 0x15;
static constexpr uint8_t apple_delete = 0x7F;
static constexpr uint8_t apple_code_mask = 0x7F;
static constexpr uint8_t control_mask = 0x1F;

static constexpr uint8_t custom_flag_active = 1;
static constexpr uint8_t custom_flag_open_apple = 2;
static constexpr uint8_t custom_flag_solid_apple = 4;
static constexpr uint8_t custom_flag_rept = 8;

static auto trim_str(const std::string& str) -> std::string {
  size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, (last - first + 1));
}

static auto to_lower_str(std::string s) -> std::string {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

namespace {

struct CustomKey {
  uint8_t normal_val = 0;
  uint8_t shift_val = 0;
  uint8_t ctrl_val = 0;
  uint8_t flags = 0;
};

}  // namespace

static std::array<CustomKey, keyb_map_size> custom_keys{};
static bool caps_lock = true;
static int caps_mode = caps_mode_host;
static KeyboardMappingMode mapping_mode = kbd_mode_symbolic;
static uint8_t keyboard_layout = keyboard_layout_us;

static auto layout_table(uint8_t layout) -> const Apple2KeyboardMap_t* {
  static const std::array<const Apple2KeyboardMap_t*, 12> tables = {
      &map_us, &map_uk, &map_fr, &map_de, &map_es,       &map_it,
      &map_se, &map_dk, &map_ch, &map_ca, &map_jp_roman, &map_jp_kana,
  };
  if (layout == keyboard_layout_us || layout >= tables.size()) {
    return nullptr;
  }
  return tables.at(layout);
}

static auto is_lower_letter(uint32_t code) -> bool {
  return code >= 'a' && code <= 'z';
}

// Shift takes the table's column where it has one, else the upper-case fold;
// CAPS LOCK folds the unshifted letters (Apple IIe Technical Reference Manual,
// Table 2-3); control clears bits 5 and 6 unless the table says otherwise.
static auto resolve(uint32_t base, uint32_t shift_val, uint32_t ctrl_val,
                    const KeyboardHostKey* key, uint8_t* apple_code) -> bool {
  if (base == 0) {
    return false;
  }
  if (key->shift) {
    if (shift_val != 0) {
      base = shift_val;
    } else if (is_lower_letter(base)) {
      base = base - 'a' + 'A';
    }
  } else if (caps_lock && is_lower_letter(base)) {
    base = base - 'a' + 'A';
  }
  if (key->ctrl) {
    base = ctrl_val != 0 ? ctrl_val : (base & control_mask);
  }
  *apple_code = static_cast<uint8_t>(base & apple_code_mask);
  return true;
}

static auto translate_positional(const KeyboardHostKey* key,
                                 uint8_t* apple_code) -> bool {
  const uint32_t idx = key->scancode;
  const Apple2KeyboardMap_t* layout =
      linapple_get_rocker_switch() ? layout_table(keyboard_layout) : nullptr;
  if (layout != nullptr && layout->map[idx] != 0) {
    return resolve(layout->map[idx], layout->shift_map[idx],
                   layout->ctrl_map[idx], key, apple_code);
  }
  return resolve(map_us.map[idx], map_us.shift_map[idx], map_us.ctrl_map[idx],
                 key, apple_code);
}

// The US shift pairs as printed on the //e keycaps (Apple IIe Technical
// Reference Manual, Figure 2-1); a host of another layout still sends the
// key's unshifted ASCII.
static auto shifted_symbol(uint32_t key) -> uint32_t {
  switch (key) {
    case '1':
      return '!';
    case '2':
      return '@';
    case '3':
      return '#';
    case '4':
      return '$';
    case '5':
      return '%';
    case '6':
      return '^';
    case '7':
      return '&';
    case '8':
      return '*';
    case '9':
      return '(';
    case '0':
      return ')';
    case '-':
      return '_';
    case '=':
      return '+';
    case '[':
      return '{';
    case ']':
      return '}';
    case '\\':
      return '|';
    case ';':
      return ':';
    case '\'':
      return '"';
    case '`':
      return '~';
    case ',':
      return '<';
    case '.':
      return '>';
    case '/':
      return '?';
    default:
      return 0;
  }
}

static auto translate_symbolic(const KeyboardHostKey* key, uint8_t* apple_code)
    -> bool {
  const uint32_t k = key->keycode;
  if (k >= static_cast<uint32_t>(ascii_printable_min) &&
      k <= static_cast<uint32_t>(ascii_printable_max)) {
    return resolve(k, shifted_symbol(k), 0, key, apple_code);
  }
  if (k != 0 && k < static_cast<uint32_t>(ascii_printable_min)) {
    *apple_code = static_cast<uint8_t>(k);
    return true;
  }
  switch (k) {
    case ascii_del:
    case linapple_key_delete:
      *apple_code = apple_delete;
      return true;
    case linapple_key_up:
      *apple_code = apple_up;
      return true;
    case linapple_key_down:
      *apple_code = apple_down;
      return true;
    case linapple_key_left:
      *apple_code = apple_left;
      return true;
    case linapple_key_right:
      *apple_code = apple_right;
      return true;
    default:
      return false;
  }
}

}  // namespace keyboard_translator

auto keyboard_translate(const KeyboardHostKey* key, uint8_t* apple_code)
    -> bool {
  namespace kt = keyboard_translator;
  if (key == nullptr || apple_code == nullptr) {
    return false;
  }
  const bool has_scancode =
      key->scancode != keyb_idx_unknown && key->scancode < keyb_map_size;
  if (has_scancode) {
    const kt::CustomKey& custom = kt::custom_keys.at(key->scancode);
    if ((custom.flags & kt::custom_flag_active) != 0) {
      if (keyboard_custom_switch(key->scancode) !=
          keyboard_custom_switch_none) {
        return false;
      }
      return kt::resolve(custom.normal_val, custom.shift_val, custom.ctrl_val,
                         key, apple_code);
    }
    if (kt::mapping_mode == kbd_mode_positional) {
      return kt::translate_positional(key, apple_code);
    }
  }
  return kt::translate_symbolic(key, apple_code);
}

auto keyboard_custom_switch(uint32_t scancode) -> KeyboardCustomSwitch {
  namespace kt = keyboard_translator;
  if (scancode >= keyb_map_size) {
    return keyboard_custom_switch_none;
  }
  const uint8_t flags = kt::custom_keys.at(scancode).flags;
  if ((flags & kt::custom_flag_active) == 0) {
    return keyboard_custom_switch_none;
  }
  if ((flags & kt::custom_flag_open_apple) != 0) {
    return keyboard_custom_switch_open_apple;
  }
  if ((flags & kt::custom_flag_solid_apple) != 0) {
    return keyboard_custom_switch_solid_apple;
  }
  if ((flags & kt::custom_flag_rept) != 0) {
    return keyboard_custom_switch_rept;
  }
  return keyboard_custom_switch_none;
}

auto keyboard_symbolic_to_core(int key, uint32_t mod) -> LinAppleKey {
  (void)mod;

  namespace kt = keyboard_translator;

  if (key >= kt::ascii_printable_min && key <= kt::ascii_printable_max) {
    return static_cast<LinAppleKey>(key);
  }

  switch (key) {
    case kt::ascii_cr:
      return linapple_key_return;
    case kt::ascii_esc:
      return linapple_key_escape;
    case kt::ascii_bs:
      return linapple_key_backspace;
    case kt::ascii_tab:
      return linapple_key_tab;
    case kt::ascii_del:
      return linapple_key_delete;
    default:
      return linapple_key_unknown;
  }
}

auto keyboard_set_caps(bool on) -> void { keyboard_translator::caps_lock = on; }

auto keyboard_get_caps() -> bool { return keyboard_translator::caps_lock; }

auto keyboard_get_caps_mode() -> int { return keyboard_translator::caps_mode; }

auto keyboard_set_caps_mode(int mode) -> void {
  keyboard_translator::caps_mode = mode;
}

auto keyboard_set_mapping_mode(KeyboardMappingMode mode) -> void {
  keyboard_translator::mapping_mode = mode;
}

auto keyboard_get_mapping_mode() -> KeyboardMappingMode {
  return keyboard_translator::mapping_mode;
}

auto keyboard_set_layout(uint8_t layout) -> void {
  keyboard_translator::keyboard_layout = layout;
}

auto keyboard_get_layout() -> uint8_t {
  return keyboard_translator::keyboard_layout;
}

auto keyboard_parse_host_key(const char* name) -> uint32_t {
  if (name == nullptr) {
    return keyb_idx_unknown;
  }

  std::string s =
      keyboard_translator::to_lower_str(keyboard_translator::trim_str(name));
  if (s.empty()) {
    return keyb_idx_unknown;
  }

  if (s.length() == 1) {
    char c = s[0];
    if (c >= 'a' && c <= 'z') {
      return keyb_idx_a + (c - 'a');
    }
    if (c >= '1' && c <= '9') {
      return keyb_idx_1 + (c - '1');
    }
    if (c == '0') {
      return keyb_idx_0;
    }
    if (c == '-') {
      return keyb_idx_minus;
    }
    if (c == '=') {
      return keyb_idx_equals;
    }
    if (c == '[') {
      return keyb_idx_leftbracket;
    }
    if (c == ']') {
      return keyb_idx_rightbracket;
    }
    if (c == '\\') {
      return keyb_idx_backslash;
    }
    if (c == ';') {
      return keyb_idx_semicolon;
    }
    if (c == '\'') {
      return keyb_idx_apostrophe;
    }
    if (c == '`') {
      return keyb_idx_grave;
    }
    if (c == ',') {
      return keyb_idx_comma;
    }
    if (c == '.') {
      return keyb_idx_period;
    }
    if (c == '/') {
      return keyb_idx_slash;
    }
    if (c == ' ') {
      return keyb_idx_space;
    }
  }

  if (s == "return" || s == "enter") {
    return keyb_idx_return;
  }
  if (s == "escape" || s == "esc") {
    return keyb_idx_escape;
  }
  if (s == "backspace" || s == "bs") {
    return keyb_idx_backspace;
  }
  if (s == "tab") {
    return keyb_idx_tab;
  }
  if (s == "space" || s == "spacebar") {
    return keyb_idx_space;
  }
  if (s == "minus") {
    return keyb_idx_minus;
  }
  if (s == "equals" || s == "equal") {
    return keyb_idx_equals;
  }
  if (s == "leftbracket" || s == "bracketleft") {
    return keyb_idx_leftbracket;
  }
  if (s == "rightbracket" || s == "bracketright") {
    return keyb_idx_rightbracket;
  }
  if (s == "backslash") {
    return keyb_idx_backslash;
  }
  if (s == "semicolon") {
    return keyb_idx_semicolon;
  }
  if (s == "apostrophe" || s == "quote") {
    return keyb_idx_apostrophe;
  }
  if (s == "grave" || s == "backquote") {
    return keyb_idx_grave;
  }
  if (s == "comma") {
    return keyb_idx_comma;
  }
  if (s == "period" || s == "dot") {
    return keyb_idx_period;
  }
  if (s == "slash") {
    return keyb_idx_slash;
  }
  if (s == "caps" || s == "capslock" || s == "caps lock") {
    return keyb_idx_capslock;
  }

  if (s == "up" || s == "uparrow" || s == "up arrow") {
    return keyb_idx_up;
  }
  if (s == "down" || s == "downarrow" || s == "down arrow") {
    return keyb_idx_down;
  }
  if (s == "left" || s == "leftarrow" || s == "left arrow") {
    return keyb_idx_left;
  }
  if (s == "right" || s == "rightarrow" || s == "right arrow") {
    return keyb_idx_right;
  }

  if (s.length() >= 2 && s[0] == 'f') {
    try {
      int fnum = std::stoi(s.substr(1));
      if (fnum >= 1 && fnum <= 12) {
        return keyb_idx_f1 + (fnum - 1);
      }
    } catch (const std::exception&) {
      return keyb_idx_unknown;
    }
  }

  return keyb_idx_unknown;
}

auto keyboard_parse_apple2_val(const char* name, uint8_t* out_flags)
    -> uint8_t {
  namespace kt = keyboard_translator;
  if (out_flags != nullptr) {
    *out_flags = 0;
  }
  if (name == nullptr) {
    return 0;
  }

  std::string s = kt::to_lower_str(kt::trim_str(name));
  if (s.empty()) {
    return 0;
  }

  if (s == "openapple" || s == "open apple" || s == "open_apple" || s == "oa") {
    if (out_flags != nullptr) {
      *out_flags |= kt::custom_flag_open_apple;
    }
    return 0;
  }
  if (s == "closedapple" || s == "closed apple" || s == "closed_apple" ||
      s == "solidapple" || s == "solid apple" || s == "ca") {
    if (out_flags != nullptr) {
      *out_flags |= kt::custom_flag_solid_apple;
    }
    return 0;
  }
  if (s == "rept" || s == "repeat") {
    if (out_flags != nullptr) {
      *out_flags |= kt::custom_flag_rept;
    }
    return 0;
  }

  if (s == "up" || s == "uparrow" || s == "up arrow") {
    return kt::apple_up;
  }
  if (s == "down" || s == "downarrow" || s == "down arrow") {
    return kt::apple_down;
  }
  if (s == "left" || s == "leftarrow" || s == "left arrow") {
    return kt::apple_left;
  }
  if (s == "right" || s == "rightarrow" || s == "right arrow") {
    return kt::apple_right;
  }
  if (s == "return" || s == "enter") {
    return kt::ascii_cr;
  }
  if (s == "escape" || s == "esc") {
    return kt::ascii_esc;
  }
  if (s == "backspace" || s == "bs") {
    return kt::ascii_del;
  }
  if (s == "delete" || s == "del") {
    return kt::ascii_del;
  }
  if (s == "tab") {
    return kt::ascii_tab;
  }
  if (s == "space" || s == "spacebar") {
    return ' ';
  }

  // Hex values like 0x0B or $15
  if ((s.length() > 2 && (s.rfind("0x", 0) == 0)) ||
      (s.length() > 1 && s[0] == '$')) {
    try {
      std::string hex_str = (s[0] == '$') ? s.substr(1) : s.substr(2);
      const uint64_t val = std::stoul(hex_str, nullptr, 16);
      return static_cast<uint8_t>(val & 0xFF);
    } catch (const std::exception&) {
      return 0;
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

  // The code points the national character generators give these glyphs.
  if (s == "ä" || s == "é") {
    return 0x7B;
  }
  if (s == "ö" || s == "ù") {
    return 0x7C;
  }
  if (s == "ü" || s == "è") {
    return 0x7D;
  }
  if (s == "°") {
    return 0x5B;
  }
  if (s == "ç") {
    return 0x5C;
  }
  if (s == "§") {
    return 0x5D;
  }
  if (s == "£") {
    return 0x23;
  }

  return 0;
}

auto keyboard_apply_custom_mappings() -> void {
  namespace kt = keyboard_translator;
  kt::custom_keys.fill(kt::CustomKey{});

  const auto* custom_section =
      Configuration::instance().get_section("Keyboard.Custom");
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
      tokens.push_back(kt::trim_str(token));
    }

    if (tokens.empty()) {
      continue;
    }

    kt::CustomKey custom;
    custom.flags = kt::custom_flag_active;

    uint8_t flags0 = 0;
    custom.normal_val = keyboard_parse_apple2_val(tokens[0].c_str(), &flags0);
    custom.flags |= flags0;

    if (tokens.size() > 1) {
      uint8_t flags1 = 0;
      custom.shift_val = keyboard_parse_apple2_val(tokens[1].c_str(), &flags1);
      custom.flags |= flags1;
    } else if (kt::is_lower_letter(custom.normal_val)) {
      custom.shift_val = custom.normal_val - 'a' + 'A';
    } else {
      custom.shift_val = custom.normal_val;
    }

    if (tokens.size() > 2) {
      uint8_t flags2 = 0;
      custom.ctrl_val = keyboard_parse_apple2_val(tokens[2].c_str(), &flags2);
      custom.flags |= flags2;
    } else if (custom.normal_val != 0) {
      custom.ctrl_val = custom.normal_val & kt::control_mask;
    }

    kt::custom_keys.at(scancode) = custom;
  }
}

auto keyboard_has_custom_mappings() -> bool {
  const auto* custom_section =
      Configuration::instance().get_section("Keyboard.Custom");
  return (custom_section != nullptr && !custom_section->empty());
}

auto frontend_update_keyboard_mapping() -> void {
  uint32_t mode = 0;
  if (config_load_int("Keyboard", "Mapping Mode", &mode)) {
    keyboard_set_mapping_mode(mode == kbd_mode_positional ? kbd_mode_positional
                                                          : kbd_mode_symbolic);
  }

  uint32_t caps_mode = 0;
  if (config_load_int("Keyboard", "Caps Lock Mode", &caps_mode)) {
    keyboard_set_caps_mode(static_cast<int>(caps_mode));
  }

  uint32_t layout = 0;
  if (config_load_int("Configuration", "Keyboard Type", &layout)) {
    keyboard_set_layout(static_cast<uint8_t>(layout));
  }

  uint32_t rocker = 0;
  if (config_load_int("Configuration", "Keyboard Rocker Switch", &rocker)) {
    linapple_set_rocker_switch(rocker != 0);
  }

  std::string qs_mod;
  if (config_load_string("Keyboard", "Quick Save Modifier", &qs_mod)) {
    for (char& c : qs_mod) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (qs_mod == "ctrl" || qs_mod == "control") {
      keyboard_set_quicksave_mode(quicksave_mode_ctrl);
    } else if (qs_mod == "altctrl" || qs_mod == "ctrlalt" ||
               qs_mod == "alt+ctrl" || qs_mod == "ctrl+alt") {
      keyboard_set_quicksave_mode(quicksave_mode_alt_ctrl);
    } else if (qs_mod == "none" || qs_mod == "disabled" || qs_mod == "0" ||
               qs_mod == "off") {
      keyboard_set_quicksave_mode(quicksave_mode_disabled);
    } else {
      keyboard_set_quicksave_mode(quicksave_mode_alt);
    }
  }

  uint32_t hotkeys_val = 1;
  if (config_load_int("Keyboard", "Enable Hotkeys", &hotkeys_val) ||
      config_load_int("Keyboard", "Function Keys Enable", &hotkeys_val)) {
    keyboard_set_hotkeys_enabled(hotkeys_val != 0);
  }

  keyboard_apply_custom_mappings();
}

static QuickSaveMode quicksave_mode = quicksave_mode_alt;
static bool hotkeys_enabled = true;

auto keyboard_get_quicksave_mode() -> QuickSaveMode { return quicksave_mode; }

auto keyboard_set_quicksave_mode(QuickSaveMode mode) -> void {
  quicksave_mode = mode;
}

auto keyboard_get_hotkeys_enabled() -> bool { return hotkeys_enabled; }

auto keyboard_set_hotkeys_enabled(bool enabled) -> void {
  hotkeys_enabled = enabled;
}

auto keyboard_is_quicksave_combo(uint32_t sym, uint32_t mod, int* out_slot,
                                 bool* out_is_save) -> bool {
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
  switch (quicksave_mode) {
    case quicksave_mode_alt:
      triggered = has_alt && !has_ctrl;
      break;
    case quicksave_mode_ctrl:
      triggered = has_ctrl && !has_alt;
      break;
    case quicksave_mode_alt_ctrl:
      triggered = has_alt && has_ctrl;
      break;
    case quicksave_mode_disabled:
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
