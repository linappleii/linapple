// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl3/SDL_Video.h"

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "SdlBackend.h"
#include "apple2/Video.h"
#include "frontends/common/VideoSurface.h"

auto stretch_blt_mem_to_frame_dc() -> void {
  const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
  video_set_frame_ready(true);
}

ScopedSurfaceLock::ScopedSurfaceLock(SDL_Surface* surface) : surface_(surface) {
  if (surface_ == nullptr) {
    return;
  }
  locked_ = sdl_compat_lock_surface(surface_);
  constexpr int default_bpp = 4;
  const int bpp = SDL_BYTESPERPIXEL(surface_->format);
  bpp_ = (bpp > 0) ? bpp : default_bpp;
  SDL_Palette* palette = SDL_GetSurfacePalette(surface_);
  if (palette != nullptr && palette->colors != nullptr) {
    const int ncolors = std::max(
        0, std::min(palette->ncolors, static_cast<int>(video_palette_size)));
    for (int i = 0; i < ncolors; ++i) {
      const auto idx = static_cast<size_t>(i);
      palette_.at(idx).r = palette->colors[i].r;
      palette_.at(idx).g = palette->colors[i].g;
      palette_.at(idx).b = palette->colors[i].b;
      palette_.at(idx).a = palette->colors[i].a;
    }
    has_palette_ = true;
  }
}

ScopedSurfaceLock::~ScopedSurfaceLock() {
  if (locked_ && surface_ != nullptr) {
    sdl_compat_unlock_surface(surface_);
  }
}

ScopedSurfaceLock::ScopedSurfaceLock(ScopedSurfaceLock&& other) noexcept
    : surface_(other.surface_),
      locked_(other.locked_),
      bpp_(other.bpp_),
      has_palette_(other.has_palette_),
      palette_(other.palette_) {
  other.surface_ = nullptr;
  other.locked_ = false;
  other.has_palette_ = false;
}

auto ScopedSurfaceLock::operator=(ScopedSurfaceLock&& other) noexcept
    -> ScopedSurfaceLock& {
  if (this != &other) {
    if (locked_ && surface_ != nullptr) {
      sdl_compat_unlock_surface(surface_);
    }
    surface_ = other.surface_;
    locked_ = other.locked_;
    bpp_ = other.bpp_;
    has_palette_ = other.has_palette_;
    palette_ = other.palette_;
    other.surface_ = nullptr;
    other.locked_ = false;
    other.has_palette_ = false;
  }
  return *this;
}

auto ScopedSurfaceLock::view() const noexcept -> VideoSurfaceView {
  if (surface_ == nullptr || surface_->pixels == nullptr) {
    return {};
  }
  return VideoSurfaceView{static_cast<uint8_t*>(surface_->pixels),
                          surface_->w,
                          surface_->h,
                          surface_->pitch,
                          bpp_,
                          has_palette_ ? palette_.data() : nullptr};
}

auto sdl_surface_to_video_surface(SDL_Surface* surface) -> VideoSurface {
  ScopedSurfaceLock lock(surface);
  VideoSurface video_surface{};
  const VideoSurfaceView v = lock.view();
  if (v.pixels == nullptr) {
    return video_surface;
  }
  video_surface.pixels = v.pixels;
  video_surface.w = v.w;
  video_surface.h = v.h;
  video_surface.pitch = v.pitch;
  video_surface.bpp = v.bpp;
  if (v.palette != nullptr) {
    std::copy(v.palette, v.palette + video_palette_size,
              video_surface.palette.begin());
  }
  return video_surface;
}
