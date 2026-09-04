// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include <memory>

struct SdlWindowDeleter_t {
  void operator()(SDL_Window* ptr) const noexcept {
    if (ptr != nullptr) {
      SDL_DestroyWindow(ptr);
    }
  }
};

struct SdlRendererDeleter_t {
  void operator()(SDL_Renderer* ptr) const noexcept {
    if (ptr != nullptr) {
      SDL_DestroyRenderer(ptr);
    }
  }
};

struct SdlTextureDeleter_t {
  void operator()(SDL_Texture* ptr) const noexcept {
    if (ptr != nullptr) {
      SDL_DestroyTexture(ptr);
    }
  }
};

struct SdlSurfaceDeleter_t {
  void operator()(SDL_Surface* ptr) const noexcept {
    if (ptr != nullptr) {
      SDL_DestroySurface(ptr);
    }
  }
};

struct SdlJoystickDeleter_t {
  void operator()(SDL_Joystick* ptr) const noexcept {
    if (ptr != nullptr) {
      SDL_CloseJoystick(ptr);
    }
  }
};

using SdlWindowPtr_t = std::unique_ptr<SDL_Window, SdlWindowDeleter_t>;
using SdlRendererPtr_t = std::unique_ptr<SDL_Renderer, SdlRendererDeleter_t>;
using SdlTexturePtr_t = std::unique_ptr<SDL_Texture, SdlTextureDeleter_t>;
using SdlSurfacePtr_t = std::unique_ptr<SDL_Surface, SdlSurfaceDeleter_t>;
using SdlJoystickPtr_t = std::unique_ptr<SDL_Joystick, SdlJoystickDeleter_t>;
