// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL/SDL.h>

#include <memory>

struct SdlSurfaceDeleter_t {
  void operator()(SDL_Surface* ptr) const {
    if (ptr != nullptr) {
      SDL_FreeSurface(ptr);
    }
  }
};

struct SdlJoystickDeleter_t {
  void operator()(SDL_Joystick* ptr) const {
    if (ptr != nullptr) {
      SDL_JoystickClose(ptr);
    }
  }
};

using SdlSurfacePtr_t = std::unique_ptr<SDL_Surface, SdlSurfaceDeleter_t>;
using SdlJoystickPtr_t = std::unique_ptr<SDL_Joystick, SdlJoystickDeleter_t>;
