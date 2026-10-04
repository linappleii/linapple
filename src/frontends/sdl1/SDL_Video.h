// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>

#include "SdlBackend.h"
#include "frontends/common/VideoSurface.h"

// RAII guard that manages surface locking for direct pixel access and exposes
// a non-owning VideoSurfaceView_t valid for the duration of the lock's
// lifetime.
class ScopedSurfaceLock_t {
 public:
  explicit ScopedSurfaceLock_t(SDL_Surface* surface);
  ~ScopedSurfaceLock_t();

  ScopedSurfaceLock_t(const ScopedSurfaceLock_t&) = delete;
  auto operator=(const ScopedSurfaceLock_t&) -> ScopedSurfaceLock_t& = delete;
  ScopedSurfaceLock_t(ScopedSurfaceLock_t&& other) noexcept;
  auto operator=(ScopedSurfaceLock_t&& other) noexcept -> ScopedSurfaceLock_t&;

  [[nodiscard]] auto view() const noexcept -> VideoSurfaceView_t;
  operator VideoSurfaceView_t() const noexcept { return view(); }
  [[nodiscard]] auto surface() const noexcept -> SDL_Surface* {
    return surface_;
  }
  [[nodiscard]] auto is_valid() const noexcept -> bool {
    return surface_ != nullptr && surface_->pixels != nullptr;
  }

 private:
  SDL_Surface* surface_ = nullptr;
  bool locked_ = false;
  int bpp_ = 0;
  bool has_palette_ = false;
  std::array<VideoColor_t, k_video_palette_size> palette_{};
};

// Deprecated: Use ScopedSurfaceLock_t to obtain a safe, RAII-scoped
// VideoSurfaceView_t.
auto sdl_surface_to_video_surface(SDL_Surface* surface) -> VideoSurface_t;
