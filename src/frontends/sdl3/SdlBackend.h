// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL3/SDL.h>           // IWYU pragma: export
#include <SDL3/SDL_events.h>    // IWYU pragma: export
#include <SDL3/SDL_init.h>      // IWYU pragma: export
#include <SDL3/SDL_joystick.h>  // IWYU pragma: export
#include <SDL3/SDL_keyboard.h>  // IWYU pragma: export
#include <SDL3/SDL_keycode.h>   // IWYU pragma: export
#include <SDL3/SDL_timer.h>     // IWYU pragma: export
#include <SDL3/SDL_video.h>     // IWYU pragma: export

#include <cstdint>

#include "frontends/sdl3/SdlPtr.h"  // IWYU pragma: export

using SdlKeycode_t = SDL_Keycode;
using SdlKeymod_t = SDL_Keymod;

constexpr auto SDL_COMPAT_QUIT = SDL_EVENT_QUIT;

constexpr auto KMOD_SHIFT = SDL_KMOD_SHIFT;
constexpr auto KMOD_CTRL = SDL_KMOD_CTRL;
constexpr auto KMOD_ALT = SDL_KMOD_ALT;
constexpr auto KMOD_GUI = SDL_KMOD_GUI;
constexpr auto KMOD_CAPS = SDL_KMOD_CAPS;
constexpr auto KMOD_MODE = SDL_KMOD_MODE;
constexpr auto KMOD_NONE = SDL_KMOD_NONE;

constexpr auto SDL_COMPAT_KMOD_SHIFT = SDL_KMOD_SHIFT;
constexpr auto SDL_COMPAT_KMOD_CTRL = SDL_KMOD_CTRL;
constexpr auto SDL_COMPAT_KMOD_ALT = SDL_KMOD_ALT;
constexpr auto SDL_COMPAT_KMOD_GUI = SDL_KMOD_GUI;

extern SdlWindowPtr_t g_window;

inline auto sdl_compat_get_key_from_event(const SDL_Event& event)
    -> SdlKeycode_t {
  return event.key.key;
}

inline auto sdl_compat_set_window_title(const char* title) -> void {
  if (g_window != nullptr) {
    SDL_SetWindowTitle(g_window.get(), title);
  }
}

inline auto sdl_compat_num_joysticks() -> int {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetJoysticks(&count);
  if (ids != nullptr) {
    SDL_free(ids);
  }
  return count;
}

inline auto sdl_compat_open_joystick(int index) -> SdlJoystickPtr_t {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetJoysticks(&count);
  if (ids == nullptr) {
    return nullptr;
  }
  SDL_Joystick* joy = nullptr;
  if (index >= 0 && index < count) {
    joy = SDL_OpenJoystick(ids[index]);
  }
  SDL_free(ids);
  return SdlJoystickPtr_t(joy);
}

inline auto sdl_compat_update_joysticks() -> void { SDL_UpdateJoysticks(); }

inline auto sdl_compat_get_joystick_button(SDL_Joystick* joy, int button)
    -> bool {
  if (joy == nullptr) {
    return false;
  }
  return SDL_GetJoystickButton(joy, button);
}

inline auto sdl_compat_get_joystick_axis(SDL_Joystick* joy, int axis)
    -> int16_t {
  if (joy == nullptr) {
    return 0;
  }
  return SDL_GetJoystickAxis(joy, axis);
}
