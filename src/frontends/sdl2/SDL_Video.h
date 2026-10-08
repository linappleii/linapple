// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL2/SDL_surface.h>

#include <array>

#include "frontends/common/VideoSurface.h"

auto stretch_blt_mem_to_frame_dc() -> void;

// RAII guard that manages surface locking for direct pixel access and exposes
// a non-owning VideoSurfaceView valid for the duration of the lock's
// lifetime.
class ScopedSurfaceLock {
 public:
  explicit ScopedSurfaceLock(SDL_Surface* surface);
  ~ScopedSurfaceLock();

  ScopedSurfaceLock(const ScopedSurfaceLock&) = delete;
  auto operator=(const ScopedSurfaceLock&) -> ScopedSurfaceLock& = delete;
  ScopedSurfaceLock(ScopedSurfaceLock&& other) noexcept;
  auto operator=(ScopedSurfaceLock&& other) noexcept -> ScopedSurfaceLock&;

  auto view() const noexcept -> VideoSurfaceView;
  operator VideoSurfaceView() const noexcept { return view(); }
  auto surface() const noexcept -> SDL_Surface* { return surface_; }
  auto is_valid() const noexcept -> bool {
    return surface_ != nullptr && surface_->pixels != nullptr;
  }

 private:
  SDL_Surface* surface_ = nullptr;
  bool locked_ = false;
  int bpp_ = 0;
  bool has_palette_ = false;
  std::array<VideoColor, video_palette_size> palette_{};
};

// Deprecated: Use ScopedSurfaceLock to obtain a safe, RAII-scoped
// VideoSurfaceView.
auto sdl_surface_to_video_surface(SDL_Surface* surface) -> VideoSurface;
