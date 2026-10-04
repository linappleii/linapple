// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL/SDL.h>           // IWYU pragma: export
#include <SDL/SDL_events.h>    // IWYU pragma: export
#include <SDL/SDL_joystick.h>  // IWYU pragma: export
#include <SDL/SDL_keyboard.h>  // IWYU pragma: export
#include <SDL/SDL_keysym.h>    // IWYU pragma: export
#include <SDL/SDL_timer.h>     // IWYU pragma: export
#include <SDL/SDL_video.h>     // IWYU pragma: export

#include <cstdint>

#include "frontends/sdl1/SdlPtr.h"  // IWYU pragma: export

using SdlKeycode_t = SDLKey;
using SdlKeymod_t = SDLMod;

constexpr auto SDL_COMPAT_QUIT = SDL_QUIT;

constexpr auto SDLK_KP_1 = SDLK_KP1;
constexpr auto SDLK_KP_2 = SDLK_KP2;
constexpr auto SDLK_KP_3 = SDLK_KP3;
constexpr auto SDLK_KP_4 = SDLK_KP4;
constexpr auto SDLK_KP_5 = SDLK_KP5;
constexpr auto SDLK_KP_6 = SDLK_KP6;
constexpr auto SDLK_KP_7 = SDLK_KP7;
constexpr auto SDLK_KP_8 = SDLK_KP8;
constexpr auto SDLK_KP_9 = SDLK_KP9;
constexpr auto SDLK_KP_0 = SDLK_KP0;

constexpr auto SDLK_LGUI = SDLK_LMETA;
constexpr auto SDLK_RGUI = SDLK_RMETA;

constexpr auto KMOD_GUI = KMOD_META;

constexpr auto SDL_COMPAT_KMOD_SHIFT = KMOD_SHIFT;
constexpr auto SDL_COMPAT_KMOD_CTRL = KMOD_CTRL;
constexpr auto SDL_COMPAT_KMOD_ALT = KMOD_ALT;
constexpr auto SDL_COMPAT_KMOD_GUI = KMOD_META;

inline auto sdl_compat_get_key_from_event(const SDL_Event& event)
    -> SdlKeycode_t {
  return event.key.keysym.sym;
}

inline auto sdl_compat_set_window_title(const char* title) -> void {
  SDL_WM_SetCaption(title, title);
}

inline auto sdl_compat_num_joysticks() -> int { return SDL_NumJoysticks(); }

inline auto sdl_compat_open_joystick(int index) -> SdlJoystickPtr_t {
  return SdlJoystickPtr_t(SDL_JoystickOpen(index));
}

inline auto sdl_compat_update_joysticks() -> void { SDL_JoystickUpdate(); }

inline auto sdl_compat_get_joystick_button(SDL_Joystick* joy, int button)
    -> bool {
  if (joy == nullptr) {
    return false;
  }
  return SDL_JoystickGetButton(joy, button) != 0;
}

inline auto sdl_compat_get_joystick_axis(SDL_Joystick* joy, int axis)
    -> int16_t {
  if (joy == nullptr) {
    return 0;
  }
  return SDL_JoystickGetAxis(joy, axis);
}

inline auto sdl_compat_lock_surface(SDL_Surface* s) -> bool {
  if (s == nullptr) {
    return false;
  }
  if (!SDL_MUSTLOCK(s)) {
    return true;
  }
  return SDL_LockSurface(s) == 0;
}

inline auto sdl_compat_unlock_surface(SDL_Surface* s) -> void {
  if (s != nullptr && SDL_MUSTLOCK(s)) {
    SDL_UnlockSurface(s);
  }
}
