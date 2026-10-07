// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "SdlBackend.h"
#include "core/LinAppleCore.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/KeyboardTranslator.h"

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

  // A switch types nothing; an Apple key only closes its side of the line.
  switch (keyboard_custom_switch(scancode)) {
    case keyboard_custom_switch_open_apple:
      g_host_modifiers.open_apple = is_down;
      send_host_modifiers();
      return;
    case keyboard_custom_switch_solid_apple:
      g_host_modifiers.solid_apple = is_down;
      send_host_modifiers();
      return;
    case keyboard_custom_switch_rept:
      send_host_modifiers();
      linapple_set_rept(is_down);
      return;
    case keyboard_custom_switch_none:
      break;
  }
  send_host_modifiers();

  const KeyboardHostKey_t key = {scancode,
                                 static_cast<uint32_t>(frontend_to_core_key(
                                     static_cast<int>(keycode), mod)),
                                 g_host_modifiers.shift, g_host_modifiers.ctrl};
  uint8_t apple_code = 0;
  if (!keyboard_translate(&key, &apple_code)) {
    return;
  }
  // A host that reports no scancode leaves the keycode as the key's identity.
  const uint32_t host_key = scancode != 0 ? scancode : keycode;
  linapple_set_key(host_key, apple_code, is_down);
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

// NOLINTBEGIN(misc-include-cleaner): KMOD_CAPS comes from SdlBackend.h
auto keyboard_sync_host_caps(uint32_t mod) -> void {
  if (keyboard_get_caps_mode() == caps_mode_host) {
    keyboard_set_caps((mod & KMOD_CAPS) != 0);
  }
}
// NOLINTEND(misc-include-cleaner)

auto keyboard_press_caps_lock(uint32_t mod) -> void {
  if (keyboard_get_caps_mode() == caps_mode_host) {
    keyboard_sync_host_caps(mod);
    return;
  }
  keyboard_set_caps(!keyboard_get_caps());
}

// Focus loss lets go of the Apple keys with the matrix keys.
auto keyboard_release_host_modifiers() -> void {
  g_host_modifiers = HostModifiers_t{};
  linapple_set_key_release_all();
}
