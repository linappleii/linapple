// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_joystick.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>

#include <cstdint>

#include "frontends/sdl2/Frame.h"

using SdlKeycode = SDL_Keycode;
using SdlKeymod = SDL_Keymod;

constexpr auto SDL_COMPAT_QUIT = SDL_QUIT;

constexpr auto SDL_COMPAT_KMOD_SHIFT = KMOD_SHIFT;
constexpr auto SDL_COMPAT_KMOD_CTRL = KMOD_CTRL;
constexpr auto SDL_COMPAT_KMOD_ALT = KMOD_ALT;
constexpr auto SDL_COMPAT_KMOD_GUI = KMOD_GUI;

inline auto sdl_compat_get_key_from_event(const SDL_Event& event)
    -> SdlKeycode {
  return event.key.keysym.sym;
}

inline auto sdl_compat_get_ticks() -> uint32_t { return SDL_GetTicks(); }

inline auto sdl_compat_get_mod_state() -> SdlKeymod {
  return SDL_GetModState();
}

inline auto sdl_compat_quit() -> void { SDL_Quit(); }

inline auto sdl_compat_set_window_title(const char* title) -> void {
  if (g_window != nullptr) {
    SDL_SetWindowTitle(g_window.get(), title);
  }
}

inline auto sdl_compat_num_joysticks() -> int { return SDL_NumJoysticks(); }

inline auto sdl_compat_open_joystick(int index) -> SdlJoystickPtr {
  return SdlJoystickPtr(SDL_JoystickOpen(index));
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
