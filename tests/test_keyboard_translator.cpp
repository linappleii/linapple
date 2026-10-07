// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "doctest.h"
#include "frontends/common/KeyboardMaps.h"
#include "frontends/common/KeyboardTranslator.h"

namespace {

constexpr uint32_t hid_a = keyb_idx_a;
constexpr uint32_t hid_b = keyb_idx_b;
constexpr uint32_t hid_minus = keyb_idx_minus;
constexpr uint32_t hid_grave = keyb_idx_grave;
constexpr uint32_t hid_f1 = keyb_idx_f1;
// A terminal reports no scancode.
constexpr uint32_t no_scancode = keyb_idx_unknown;

// The translator's state is process-wide, so each case starts from a host
// whose configuration names nothing, caps down, US, symbolic, rocker off.
struct ScopedTranslator_t {
  ScopedTranslator_t() { reset(); }
  ~ScopedTranslator_t() { reset(); }
  ScopedTranslator_t(const ScopedTranslator_t&) = delete;
  auto operator=(const ScopedTranslator_t&) -> ScopedTranslator_t& = delete;
  ScopedTranslator_t(ScopedTranslator_t&&) = delete;
  auto operator=(ScopedTranslator_t&&) -> ScopedTranslator_t& = delete;

  static auto custom(
      std::initializer_list<std::pair<const char*, const char*>> entries)
      -> void {
    Configuration_t::instance().data.erase("Keyboard.Custom");
    for (const auto& entry : entries) {
      Configuration_t::instance().set_string("Keyboard.Custom", entry.first,
                                             entry.second);
    }
    keyboard_apply_custom_mappings();
  }

 private:
  static auto reset() -> void {
    Configuration_t& config = Configuration_t::instance();
    config.data.erase("Keyboard.Custom");
    config.data.erase("Keyboard");
    config.data["Configuration"].erase("Keyboard Type");
    config.data["Configuration"].erase("Keyboard Rocker Switch");
    keyboard_apply_custom_mappings();
    keyboard_set_caps(true);
    keyboard_set_caps_mode(caps_mode_host);
    keyboard_set_layout(keyboard_layout_us);
    keyboard_set_mapping_mode(KBD_MODE_SYMBOLIC);
    keyboard_set_hotkeys_enabled(true);
    keyboard_set_quicksave_mode(QUICKSAVE_MODE_ALT);
    linapple_set_rocker_switch(false);
  }
};

auto translate(uint32_t scancode, uint32_t keycode, bool shift, bool ctrl)
    -> int {
  const KeyboardHostKey_t key = {scancode, keycode, shift, ctrl};
  uint8_t code = 0;
  if (!keyboard_translate(&key, &code)) {
    return -1;
  }
  return code;
}

}  // namespace

TEST_CASE(
    "Keyboard translator: caps lock starts down and folds letters, shift "
    "reads the US shift pairs, and control clears bits 5 and 6") {
  ScopedTranslator_t translator;
  CHECK(keyboard_get_caps());
  CHECK(translate(hid_a, 'a', false, false) == 0x41);
  keyboard_set_caps(false);
  CHECK(translate(hid_a, 'a', false, false) == 0x61);
  CHECK(translate(hid_a, 'a', true, false) == 0x41);
  CHECK(translate(keyb_idx_1, '1', true, false) == 0x21);
  CHECK(translate(keyb_idx_1, '1', false, false) == 0x31);
  CHECK(translate(keyb_idx_c, 'c', false, true) == 0x03);
  CHECK(translate(keyb_idx_c, 'c', true, true) == 0x03);
  CHECK(translate(keyb_idx_return, linapple_key_return, false, false) == 0x0D);
  CHECK(translate(keyb_idx_left, linapple_key_left, false, false) == 0x08);
  CHECK(translate(keyb_idx_up, linapple_key_up, false, false) == 0x0B);
  CHECK(translate(keyb_idx_backspace, linapple_key_delete, false, false) ==
        0x7F);
  // A function key has no Apple meaning in symbolic mode.
  CHECK(translate(hid_f1, linapple_key_f1, false, false) == -1);
  CHECK(translate(keyb_idx_capslock, linapple_key_capslock, false, false) ==
        -1);
}

TEST_CASE(
    "Keyboard translator: positional mode reads the US table by HID usage, "
    "and a key the table leaves blank types nothing") {
  ScopedTranslator_t translator;
  keyboard_set_mapping_mode(KBD_MODE_POSITIONAL);
  CHECK(translate(hid_a, 'q', false, false) == 0x41);
  keyboard_set_caps(false);
  CHECK(translate(hid_a, 'q', false, false) == 0x61);
  CHECK(translate(hid_minus, '-', true, false) == '_');
  CHECK(translate(hid_f1, linapple_key_f1, false, false) == -1);
  // A host without scancodes is read symbolically whatever the mode.
  CHECK(translate(no_scancode, 'q', false, false) == 0x71);
}

TEST_CASE(
    "Keyboard translator: a custom Apple key or REPT yields a switch and no "
    "code, a custom code applies to a blank key, and one custom entry leaves "
    "every other key as it was") {
  ScopedTranslator_t translator;
  ScopedTranslator_t::custom(
      {{"a", "OpenApple"}, {"grave", "Rept"}, {"f1", "0x0B"}});

  CHECK(translate(hid_a, 'a', false, false) == -1);
  CHECK(keyboard_custom_switch(hid_a) == keyboard_custom_switch_open_apple);
  CHECK(translate(hid_grave, '`', false, false) == -1);
  CHECK(keyboard_custom_switch(hid_grave) == keyboard_custom_switch_rept);
  CHECK(translate(hid_f1, linapple_key_f1, false, false) == 0x0B);
  CHECK(keyboard_custom_switch(hid_f1) == keyboard_custom_switch_none);
  // Scancode 5 is B positionally; the symbolic keycode X is what stays.
  CHECK(translate(hid_b, 'x', false, false) == 0x58);
  CHECK(keyboard_custom_switch(hid_b) == keyboard_custom_switch_none);
  CHECK(keyboard_custom_switch(keyb_map_size) == keyboard_custom_switch_none);

  // The override is read before the layout in positional mode too.
  keyboard_set_mapping_mode(KBD_MODE_POSITIONAL);
  CHECK(translate(hid_f1, linapple_key_f1, false, false) == 0x0B);
  CHECK(translate(hid_a, 'a', false, false) == -1);

  ScopedTranslator_t::custom({{"b", "SolidApple"}});
  CHECK(keyboard_custom_switch(hid_b) == keyboard_custom_switch_solid_apple);
  CHECK(keyboard_custom_switch(hid_a) == keyboard_custom_switch_none);
}

TEST_CASE(
    "Keyboard translator: Keyboard Type selects the national table in "
    "positional mode while the rocker is on, with the US table behind a "
    "blank entry") {
  ScopedTranslator_t translator;
  keyboard_set_mapping_mode(KBD_MODE_POSITIONAL);

  // The German table puts ß at the minus key and the French table a right
  // parenthesis; with the rocker off every table is the US one.
  keyboard_set_layout(keyboard_layout_de);
  CHECK(translate(hid_minus, '-', false, false) == '-');
  linapple_set_rocker_switch(true);
  CHECK(translate(hid_minus, '-', false, false) == 0x7E);
  CHECK(translate(hid_minus, '-', true, false) == '?');
  // The German table has no entry for the A key, so the US one answers.
  CHECK(translate(hid_a, 'a', false, false) == 0x41);
  keyboard_set_layout(keyboard_layout_fr);
  CHECK(translate(hid_minus, '-', false, false) == ')');
  CHECK(translate(hid_minus, '-', true, false) == 0x5D);
  keyboard_set_layout(keyboard_layout_us);
  CHECK(translate(hid_minus, '-', false, false) == '-');
  // A layout past the last table reads as the US one.
  keyboard_set_layout(12);
  CHECK(translate(hid_minus, '-', false, false) == '-');
  CHECK(keyboard_get_layout() == 12);
}

TEST_CASE(
    "Keyboard translator: the configuration is applied at start, Mapping "
    "Mode, Caps Lock Mode, Quick Save Modifier and Enable Hotkeys or its alias "
    "from [Keyboard], Keyboard Type and the rocker switch from "
    "[Configuration], and the custom section") {
  ScopedTranslator_t translator;
  Configuration_t& config = Configuration_t::instance();
  config.set_int("Keyboard", "Mapping Mode", 1);
  config.set_int("Keyboard", "Caps Lock Mode", 1);
  config.set_string("Keyboard", "Quick Save Modifier", "Ctrl");
  config.set_int("Keyboard", "Enable Hotkeys", 0);
  config.set_int("Configuration", "Keyboard Type", 3);
  config.set_int("Configuration", "Keyboard Rocker Switch", 1);
  config.set_string("Keyboard.Custom", "f1", "0x0B");
  frontend_update_keyboard_mapping();

  CHECK(keyboard_get_mapping_mode() == KBD_MODE_POSITIONAL);
  CHECK(keyboard_get_caps_mode() == caps_mode_emulated);
  CHECK(keyboard_get_quicksave_mode() == QUICKSAVE_MODE_CTRL);
  CHECK_FALSE(keyboard_get_hotkeys_enabled());
  CHECK(keyboard_get_layout() == keyboard_layout_de);
  CHECK(linapple_get_rocker_switch());
  CHECK(translate(hid_minus, '-', false, false) == 0x7E);
  CHECK(translate(hid_f1, linapple_key_f1, false, false) == 0x0B);

  // Function Keys Enable is the older spelling of Enable Hotkeys.
  config.data.erase("Keyboard");
  keyboard_set_hotkeys_enabled(true);
  config.set_int("Keyboard", "Function Keys Enable", 0);
  frontend_update_keyboard_mapping();
  CHECK_FALSE(keyboard_get_hotkeys_enabled());

  // A key the configuration leaves out leaves the setting alone.
  config.data.erase("Keyboard");
  keyboard_set_hotkeys_enabled(true);
  frontend_update_keyboard_mapping();
  CHECK(keyboard_get_hotkeys_enabled());
  CHECK(keyboard_get_mapping_mode() == KBD_MODE_POSITIONAL);
}
