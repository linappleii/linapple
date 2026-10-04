// SPDX-License-Identifier: GPL-2.0-only
#include <cctype>
#include <cstdint>
#include <string>

#include "SdlBackend.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/KeyboardTranslator.h"

static int g_keyboard_mapping_mode = 0;
static int g_keyboard_caps_mode = caps_mode_host;

auto keyboard_get_caps_mode() -> int { return g_keyboard_caps_mode; }
auto keyboard_set_caps_mode(int mode) -> void { g_keyboard_caps_mode = mode; }

auto frontend_update_keyboard_mapping() -> void {
  uint32_t mode = 0;
  if (config_load_int("Keyboard", "Mapping Mode", &mode)) {
    g_keyboard_mapping_mode = static_cast<int>(mode);
  }

  uint32_t caps_mode = 0;
  if (config_load_int("Keyboard", "Caps Lock Mode", &caps_mode)) {
    g_keyboard_caps_mode = static_cast<int>(caps_mode);
  }

  uint32_t layout = 0;
  if (config_load_int("Configuration", "Keyboard Type", &layout)) {
    uint8_t layout_val = static_cast<uint8_t>(layout);
    peripheral_command(0, keyboard_cmd_set_layout, &layout_val,
                       sizeof(layout_val));
  }

  uint32_t rocker = 0;
  if (config_load_int("Configuration", "Keyboard Rocker Switch", &rocker)) {
    uint8_t rocker_val = static_cast<uint8_t>(rocker);
    peripheral_command(0, keyboard_cmd_set_rocker, &rocker_val,
                       sizeof(rocker_val));
  }

  std::string qs_mod;
  if (config_load_string("Keyboard", "Quick Save Modifier", &qs_mod)) {
    for (char& c : qs_mod) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (qs_mod == "ctrl" || qs_mod == "control") {
      keyboard_set_quicksave_mode(QUICKSAVE_MODE_CTRL);
    } else if (qs_mod == "altctrl" || qs_mod == "ctrlalt" ||
               qs_mod == "alt+ctrl" || qs_mod == "ctrl+alt") {
      keyboard_set_quicksave_mode(QUICKSAVE_MODE_ALT_CTRL);
    } else if (qs_mod == "none" || qs_mod == "disabled" || qs_mod == "0" ||
               qs_mod == "off") {
      keyboard_set_quicksave_mode(QUICKSAVE_MODE_DISABLED);
    } else {
      keyboard_set_quicksave_mode(QUICKSAVE_MODE_ALT);
    }
  }

  uint32_t hotkeys_val = 1;
  if (config_load_int("Keyboard", "Enable Hotkeys", &hotkeys_val) ||
      config_load_int("Keyboard", "Function Keys Enable", &hotkeys_val)) {
    keyboard_set_hotkeys_enabled(hotkeys_val != 0);
  }

  keyboard_apply_custom_mappings();
}

// NOLINTBEGIN(misc-include-cleaner): Keycodes (SDLK_*) are provided across SDL1/2/3 backends via SdlBackend.h
auto frontend_to_core_key(int key, uint32_t mod) -> LinAppleKey_t {
  switch (key) {
    case SDLK_UP:
      return linapple_key_up;
    case SDLK_DOWN:
      return linapple_key_down;
    case SDLK_LEFT:
      return linapple_key_left;
    case SDLK_RIGHT:
      return linapple_key_right;
    default:
      break;
  }

  return keyboard_symbolic_to_core(key, mod);
}
// NOLINTEND(misc-include-cleaner)

// The host's four modifier levels. Shift and ctrl follow the SDL modifier
// mask; the two Apple keys follow their own key edges, because the mask
// folds both Alt keys into one bit and both GUI keys into another while the
// //e has two distinct switches (Apple IIe Technical Reference Manual, p. 13).
struct HostModifiers_t {
  bool shift = false;
  bool ctrl = false;
  bool open_apple = false;
  bool solid_apple = false;
};

static HostModifiers_t g_host_modifiers;

static auto send_host_modifiers() -> void {
  linapple_set_modifiers(g_host_modifiers.shift, g_host_modifiers.ctrl,
                         g_host_modifiers.open_apple,
                         g_host_modifiers.solid_apple);
}

static auto track_shift_and_ctrl(uint32_t mod) -> void {
  g_host_modifiers.shift = (mod & SDL_COMPAT_KMOD_SHIFT) != 0;
  g_host_modifiers.ctrl = (mod & SDL_COMPAT_KMOD_CTRL) != 0;
}

auto frontend_dispatch_key_event(uint32_t scancode, uint32_t keycode,
                                 uint32_t mod, bool is_down) -> void {
  track_shift_and_ctrl(mod);

  // A custom key mapped to an Apple key is a switch on the game port and
  // types nothing: sending its key event would latch a NUL with the strobe
  // set.
  const int apple_line = keyboard_custom_apple_line(scancode);
  if (apple_line == 0) {
    g_host_modifiers.open_apple = is_down;
  } else if (apple_line == 1) {
    g_host_modifiers.solid_apple = is_down;
  }
  send_host_modifiers();
  if (apple_line >= 0) {
    return;
  }

  LinAppleKey_t core_key = linapple_key_unknown;

  if (g_keyboard_mapping_mode == KBD_MODE_POSITIONAL ||
      keyboard_has_custom_mappings()) {
    core_key = keyboard_scancode_to_positional(scancode);
  } else {
    core_key = frontend_to_core_key(static_cast<int>(keycode), mod);
  }

  if (core_key == linapple_key_unknown) {
    return;
  }

  KeyboardEvent_t ev = {
      static_cast<uint32_t>(core_key),
      static_cast<uint8_t>(is_down ? 1 : 0),
      static_cast<uint8_t>(g_host_modifiers.shift ? 1 : 0),
      static_cast<uint8_t>(g_host_modifiers.ctrl ? 1 : 0),
      static_cast<uint8_t>(g_host_modifiers.solid_apple ? 1 : 0),
      static_cast<uint8_t>(g_host_modifiers.open_apple ? 1 : 0),
      {0, 0, 0}};
  peripheral_command(0, keyboard_cmd_event, &ev, sizeof(ev));
}

// Left Alt or Left GUI is Open Apple and Right Alt or Right GUI is Solid
// Apple. Alt is the key most desktops leave to applications; GUI (Super) is
// usually the window manager's and may never arrive.
// NOLINTBEGIN(misc-include-cleaner): Modifier keycodes (SDLK_*) are provided across SDL1/2/3 backends via SdlBackend.h
auto frontend_handle_key_event(SdlKeycode_t key, bool is_down) -> bool {
  switch (key) {
    case SDLK_LALT:
    case SDLK_LGUI:
      g_host_modifiers.open_apple = is_down;
      send_host_modifiers();
      return true;

    case SDLK_RALT:
    case SDLK_RGUI:
      g_host_modifiers.solid_apple = is_down;
      send_host_modifiers();
      return true;

    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
      track_shift_and_ctrl(static_cast<uint32_t>(sdl_compat_get_mod_state()));
      send_host_modifiers();
      return true;

    default:
      return false;
  }
}
// NOLINTEND(misc-include-cleaner)

auto frontend_handle_event(SdlKeycode_t key, bool is_down) -> bool {
  return frontend_handle_key_event(key, is_down);
}
