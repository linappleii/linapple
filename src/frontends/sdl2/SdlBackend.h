// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL2/SDL.h>           // IWYU pragma: export
#include <SDL2/SDL_events.h>    // IWYU pragma: export
#include <SDL2/SDL_joystick.h>  // IWYU pragma: export
#include <SDL2/SDL_keyboard.h>  // IWYU pragma: export
#include <SDL2/SDL_keycode.h>   // IWYU pragma: export
#include <SDL2/SDL_timer.h>     // IWYU pragma: export
#include <SDL2/SDL_video.h>     // IWYU pragma: export

#include <cstdint>

#include "frontends/sdl2/SdlPtr.h"  // IWYU pragma: export

using SdlKeycode_t = SDL_Keycode;
using SdlKeymod_t = SDL_Keymod;

constexpr auto SDL_COMPAT_QUIT = SDL_QUIT;

constexpr auto SDL_COMPAT_KMOD_SHIFT = KMOD_SHIFT;
constexpr auto SDL_COMPAT_KMOD_CTRL = KMOD_CTRL;
constexpr auto SDL_COMPAT_KMOD_ALT = KMOD_ALT;
constexpr auto SDL_COMPAT_KMOD_GUI = KMOD_GUI;

extern SdlWindowPtr_t g_window;

inline auto sdl_compat_get_key_from_event(const SDL_Event& event)
    -> SdlKeycode_t {
  return event.key.keysym.sym;
}

inline auto sdl_compat_set_window_title(const char* title) -> void {
  if (g_window != nullptr) {
    SDL_SetWindowTitle(g_window.get(), title);
  }
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
