// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL2/SDL_joystick.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_video.h>

#include <memory>

struct SdlWindowDeleter {
  auto operator()(SDL_Window* ptr) const noexcept -> void {
    if (ptr != nullptr) {
      SDL_DestroyWindow(ptr);
    }
  }
};

struct SdlRendererDeleter {
  auto operator()(SDL_Renderer* ptr) const noexcept -> void {
    if (ptr != nullptr) {
      SDL_DestroyRenderer(ptr);
    }
  }
};

struct SdlTextureDeleter {
  auto operator()(SDL_Texture* ptr) const noexcept -> void {
    if (ptr != nullptr) {
      SDL_DestroyTexture(ptr);
    }
  }
};

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

using SdlWindowPtr = std::unique_ptr<SDL_Window, SdlWindowDeleter>;
using SdlRendererPtr = std::unique_ptr<SDL_Renderer, SdlRendererDeleter>;
using SdlTexturePtr = std::unique_ptr<SDL_Texture, SdlTextureDeleter>;
using SdlSurfacePtr = std::unique_ptr<SDL_Surface, SdlSurfaceDeleter>;
using SdlJoystickPtr = std::unique_ptr<SDL_Joystick, SdlJoystickDeleter>;
