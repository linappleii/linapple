// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct VideoColor_t {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

struct VideoRect_t {
  int x;
  int y;
  int w;
  int h;
};

constexpr size_t k_video_palette_size = 256;
constexpr size_t VIDEO_PALETTE_SIZE = k_video_palette_size;

struct VideoSurface_t;

struct VideoSurfaceView_t {
  uint8_t* pixels = nullptr;
  int w = 0;
  int h = 0;
  int pitch = 0;
  int bpp = 0;
  const VideoColor_t* palette = nullptr;

  constexpr VideoSurfaceView_t() noexcept = default;
  constexpr VideoSurfaceView_t(void* p, int width, int height, int pt,
                               int bytes_per_pixel,
                               const VideoColor_t* pal = nullptr) noexcept
      : pixels(static_cast<uint8_t*>(p)),
        w(width),
        h(height),
        pitch(pt),
        bpp(bytes_per_pixel),
        palette(pal) {}
  VideoSurfaceView_t(std::nullptr_t) noexcept {}
  VideoSurfaceView_t(const VideoSurface_t* s) noexcept;
  VideoSurfaceView_t(VideoSurface_t* s) noexcept;
  VideoSurfaceView_t(const VideoSurface_t& s) noexcept;
  VideoSurfaceView_t(VideoSurface_t& s) noexcept;
  VideoSurfaceView_t(const VideoSurfaceView_t* v) noexcept {
    if (v != nullptr) {
      *this = *v;
    }
  }
  VideoSurfaceView_t(VideoSurfaceView_t* v) noexcept {
    if (v != nullptr) {
      *this = *v;
    }
  }
};

struct VideoSurface_t {
  std::vector<uint8_t> pixel_data{};
  uint8_t* pixels = nullptr;
  int w = 0;
  int h = 0;
  int pitch = 0;
  int bpp = 0;
  std::array<VideoColor_t, k_video_palette_size> palette{};
};

inline VideoSurfaceView_t::VideoSurfaceView_t(
    const VideoSurface_t* s) noexcept {
  if (s != nullptr) {
    pixels = s->pixels;
    w = s->w;
    h = s->h;
    pitch = s->pitch;
    bpp = s->bpp;
    palette = s->palette.data();
  }
}

inline VideoSurfaceView_t::VideoSurfaceView_t(VideoSurface_t* s) noexcept {
  if (s != nullptr) {
    pixels = s->pixels;
    w = s->w;
    h = s->h;
    pitch = s->pitch;
    bpp = s->bpp;
    palette = s->palette.data();
  }
}

inline VideoSurfaceView_t::VideoSurfaceView_t(const VideoSurface_t& s) noexcept
    : pixels(s.pixels),
      w(s.w),
      h(s.h),
      pitch(s.pitch),
      bpp(s.bpp),
      palette(s.palette.data()) {}

inline VideoSurfaceView_t::VideoSurfaceView_t(VideoSurface_t& s) noexcept
    : pixels(s.pixels),
      w(s.w),
      h(s.h),
      pitch(s.pitch),
      bpp(s.bpp),
      palette(s.palette.data()) {}

inline auto video_surface_view(const VideoSurface_t* s) noexcept
    -> VideoSurfaceView_t {
  if (s == nullptr) {
    return {};
  }
  return {s->pixels, s->w, s->h, s->pitch, s->bpp, s->palette.data()};
}

auto video_create_surface(int w, int h, int bpp) -> VideoSurface_t*;
auto video_destroy_surface(VideoSurface_t* s) -> void;
auto video_load_xpm(const char* const* xpm) -> VideoSurface_t*;
