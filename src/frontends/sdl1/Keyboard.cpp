// NOLINTBEGIN
#include "SDL/SDL.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/sdl1/Frontend.h"

static int keyboard_mapping_mode = 0;

void Frontend_UpdateKeyboardMapping() {
  uint32_t mode = 0;
  if (ConfigLoadInt("Keyboard", "Mapping Mode", &mode)) {
    keyboard_mapping_mode = static_cast<int>(mode);
  }
}

auto Frontend_ToCoreKey(int key, uint32_t mod) -> LinAppleKey {
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

  uint32_t standard_mod = 0;
  if (mod & KMOD_SHIFT) {
    standard_mod |= 0x01;
  }
  if (mod & KMOD_CTRL) {
    standard_mod |= 0x40;
  }

  return keyboard_symbolic_to_core(key, standard_mod);
}

void Frontend_DispatchKeyEvent(uint32_t scancode, uint32_t keycode,
                               uint32_t mod, bool is_down) {
  KeyboardModifiers_t mods = {static_cast<uint8_t>((mod & KMOD_SHIFT) ? 1 : 0),
                              static_cast<uint8_t>((mod & KMOD_CTRL) ? 1 : 0),
                              static_cast<uint8_t>((mod & KMOD_ALT) ? 1 : 0),
                              static_cast<uint8_t>((mod & KMOD_META) ? 1 : 0),
                              0};
  Peripheral_Command(0, keyb_cmd_set_mods, &mods, sizeof(mods));

  LinAppleKey core_key = LINAPPLE_KEY_UNKNOWN;

  if (keyboard_mapping_mode == KBD_MODE_POSITIONAL) {
    core_key = keyboard_scancode_to_positional(scancode);
  } else {
    core_key = Frontend_ToCoreKey(static_cast<int>(keycode), mod);
  }

  KeyboardEvent_t ev = {static_cast<uint32_t>(core_key),
                        static_cast<uint8_t>(is_down ? 1 : 0),
                        mods.shift, mods.ctrl, mods.alt, mods.gui};
  Peripheral_Command(0, keyb_cmd_event, &ev, sizeof(ev));
}

bool Frontend_HandleKeyEvent(SDLKey key, bool is_down) {
  switch (key) {
    case SDLK_LALT:
    case SDLK_LMETA:
      Linapple_SetAppleKey(0, is_down);
      return true;

    case SDLK_RALT:
    case SDLK_RMETA:
      Linapple_SetAppleKey(1, is_down);
      return true;

    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: {
      SDLMod mod = SDL_GetModState();
      KeyboardModifiers_t mods = {
          static_cast<uint8_t>((mod & KMOD_SHIFT) ? 1 : 0),
          static_cast<uint8_t>((mod & KMOD_CTRL) ? 1 : 0),
          static_cast<uint8_t>((mod & KMOD_ALT) ? 1 : 0),
          static_cast<uint8_t>((mod & KMOD_META) ? 1 : 0), 0};
      Peripheral_Command(0, keyb_cmd_set_mods, &mods, sizeof(mods));
      return true;
    }

    default:
      return false;
  }
}
// NOLINTEND
