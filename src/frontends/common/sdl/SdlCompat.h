// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#if !defined(LINAPPLE_SDL_VERSION)
#if defined(SDL_MAJOR_VERSION)
#define LINAPPLE_SDL_VERSION SDL_MAJOR_VERSION
#elif defined(SDL3_SDL_H)
#define LINAPPLE_SDL_VERSION 3
#elif defined(SDL2_SDL_H)
#define LINAPPLE_SDL_VERSION 2
#elif defined(SDL_SDL_H)
#define LINAPPLE_SDL_VERSION 1
#else
#define LINAPPLE_SDL_VERSION 3
#endif
#endif

#include <cstdint>

#if LINAPPLE_SDL_VERSION == 1
#include <SDL/SDL.h>            // IWYU pragma: export
#include <SDL/SDL_events.h>     // IWYU pragma: export
#include <SDL/SDL_joystick.h>   // IWYU pragma: export
#include <SDL/SDL_keyboard.h>   // IWYU pragma: export
#include <SDL/SDL_keysym.h>     // IWYU pragma: export
#include <SDL/SDL_timer.h>      // IWYU pragma: export
#include <SDL/SDL_video.h>      // IWYU pragma: export

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
  return SDL_JoystickGetButton(joy, button) != 0;
}

inline auto sdl_compat_get_joystick_axis(SDL_Joystick* joy, int axis)
    -> int16_t {
  return SDL_JoystickGetAxis(joy, axis);
}

#elif LINAPPLE_SDL_VERSION == 2
#include <SDL2/SDL.h>           // IWYU pragma: export
#include <SDL2/SDL_events.h>    // IWYU pragma: export
#include <SDL2/SDL_joystick.h>  // IWYU pragma: export
#include <SDL2/SDL_keyboard.h>  // IWYU pragma: export
#include <SDL2/SDL_keycode.h>   // IWYU pragma: export
#include <SDL2/SDL_timer.h>     // IWYU pragma: export
#include <SDL2/SDL_video.h>     // IWYU pragma: export

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
  return SDL_JoystickGetButton(joy, button) != 0;
}

inline auto sdl_compat_get_joystick_axis(SDL_Joystick* joy, int axis)
    -> int16_t {
  return SDL_JoystickGetAxis(joy, axis);
}

#elif LINAPPLE_SDL_VERSION == 3
#include <SDL3/SDL.h>           // IWYU pragma: export
#include <SDL3/SDL_events.h>    // IWYU pragma: export
#include <SDL3/SDL_init.h>      // IWYU pragma: export
#include <SDL3/SDL_joystick.h>  // IWYU pragma: export
#include <SDL3/SDL_keyboard.h>  // IWYU pragma: export
#include <SDL3/SDL_keycode.h>   // IWYU pragma: export
#include <SDL3/SDL_timer.h>     // IWYU pragma: export
#include <SDL3/SDL_video.h>     // IWYU pragma: export

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
  return SDL_GetJoystickButton(joy, button);
}

inline auto sdl_compat_get_joystick_axis(SDL_Joystick* joy, int axis)
    -> int16_t {
  return SDL_GetJoystickAxis(joy, axis);
}

#endif

// Shared frame lifecycle declarations across all SDL frontends
auto init_sdl() -> int;
auto frame_create_window() -> int;
auto frame_destroy_window() -> void;
