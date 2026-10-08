// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL/SDL_joystick.h>
#include <SDL/SDL_video.h>

#include <memory>

// Smart pointer deleters for SDL 1.2 resources.
// Note: SdlSurfacePtr manages allocated surfaces (SDL_CreateRGBSurface,
// SDL_LoadBMP), but must never be used with the screen surface from
// SDL_SetVideoMode, which is managed directly by SDL.
struct SdlSurfaceDeleter {
  auto operator()(SDL_Surface* ptr) const noexcept -> void {
    if (ptr != nullptr) {
      SDL_FreeSurface(ptr);
    }
  }
};

struct SdlJoystickDeleter {
  auto operator()(SDL_Joystick* ptr) const noexcept -> void {
    if (ptr != nullptr) {
      SDL_JoystickClose(ptr);
    }
  }
};

using SdlSurfacePtr = std::unique_ptr<SDL_Surface, SdlSurfaceDeleter>;
using SdlJoystickPtr = std::unique_ptr<SDL_Joystick, SdlJoystickDeleter>;
