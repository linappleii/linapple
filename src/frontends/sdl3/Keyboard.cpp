// NOLINTBEGIN
#include "SDL3/SDL.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/sdl3/Frontend.h"

static int keyboard_mapping_mode = 0;

void Frontend_UpdateKeyboardMapping() {
  uint32_t mode = 0;
  if (config_load_int("Keyboard", "Mapping Mode", &mode)) {
    keyboard_mapping_mode = static_cast<int>(mode);
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

  keyboard_apply_custom_mappings();
}

auto frontend_to_core_key(int key, uint32_t mod) -> LinAppleKey {
  switch (key) {
    case SDLK_UP:
      return LINAPPLE_KEY_UP;
    case SDLK_DOWN:
      return LINAPPLE_KEY_DOWN;
    case SDLK_LEFT:
      return LINAPPLE_KEY_LEFT;
    case SDLK_RIGHT:
      return LINAPPLE_KEY_RIGHT;
    default:
      break;
  }

  return keyboard_symbolic_to_core(key, mod);
}

void Frontend_DispatchKeyEvent(uint32_t scancode, uint32_t keycode,
                               uint32_t mod, bool is_down) {
  KeyboardModifiers_t mods = {
      static_cast<uint8_t>((mod & SDL_KMOD_SHIFT) ? 1 : 0),
      static_cast<uint8_t>((mod & SDL_KMOD_CTRL) ? 1 : 0),
      static_cast<uint8_t>((mod & SDL_KMOD_ALT) ? 1 : 0),
      static_cast<uint8_t>((mod & SDL_KMOD_GUI) ? 1 : 0),
      0,
      {0, 0, 0}};
  peripheral_command(0, keyboard_cmd_set_mods, &mods, sizeof(mods));

  LinAppleKey core_key = LINAPPLE_KEY_UNKNOWN;

  if (keyboard_mapping_mode == KBD_MODE_POSITIONAL ||
      keyboard_has_custom_mappings()) {
    core_key = keyboard_scancode_to_positional(scancode);
  } else {
    core_key = frontend_to_core_key(static_cast<int>(keycode), mod);
  }

  if (core_key == LINAPPLE_KEY_UNKNOWN) {
    return;
  }

  KeyboardEvent_t ev = {static_cast<uint32_t>(core_key),
                        static_cast<uint8_t>(is_down ? 1 : 0),
                        mods.shift,
                        mods.ctrl,
                        mods.alt,
                        mods.gui,
                        {0, 0, 0}};
  peripheral_command(0, keyboard_cmd_event, &ev, sizeof(ev));
}

auto frontend_handle_event(SDL_Keycode key, bool is_down) -> bool {
  switch (key) {
    case SDLK_LALT:
    case SDLK_LGUI:
      linapple_set_apple_key(0, is_down);
      return true;

    case SDLK_RALT:
    case SDLK_RGUI:
      linapple_set_apple_key(1, is_down);
      return true;

    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: {
      SDL_Keymod mod = SDL_GetModState();
      KeyboardModifiers_t mods = {
          static_cast<uint8_t>((mod & SDL_KMOD_SHIFT) ? 1 : 0),
          static_cast<uint8_t>((mod & SDL_KMOD_CTRL) ? 1 : 0),
          static_cast<uint8_t>((mod & SDL_KMOD_ALT) ? 1 : 0),
          static_cast<uint8_t>((mod & SDL_KMOD_GUI) ? 1 : 0),
          0,
          {0, 0, 0}};
      peripheral_command(0, keyboard_cmd_set_mods, &mods, sizeof(mods));
      return true;
    }

    default:
      return false;
  }
}
// NOLINTEND
