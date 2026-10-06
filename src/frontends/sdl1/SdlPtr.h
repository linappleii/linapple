// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL/SDL_joystick.h>
#include <SDL/SDL_video.h>

#include <memory>

// Smart pointer deleters for SDL 1.2 resources.
// Note: SdlSurfacePtr_t manages allocated surfaces (SDL_CreateRGBSurface,
// SDL_LoadBMP), but must never be used with the screen surface from
// SDL_SetVideoMode, which is managed directly by SDL.
struct SdlSurfaceDeleter_t {
  auto operator()(SDL_Surface* ptr) const noexcept -> void {
    if (ptr != nullptr) {
      SDL_FreeSurface(ptr);
    }
  }
};

struct SdlJoystickDeleter_t {
  auto operator()(SDL_Joystick* ptr) const noexcept -> void {
    if (ptr != nullptr) {
      SDL_JoystickClose(ptr);
    }
  }
};

using SdlSurfacePtr_t = std::unique_ptr<SDL_Surface, SdlSurfaceDeleter_t>;
using SdlJoystickPtr_t = std::unique_ptr<SDL_Joystick, SdlJoystickDeleter_t>;
