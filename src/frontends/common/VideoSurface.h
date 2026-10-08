// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct VideoColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

struct VideoRect {
  int x;
  int y;
  int w;
  int h;
};

constexpr size_t video_palette_size = 256;

struct VideoSurface;

struct VideoSurfaceView {
  uint8_t* pixels = nullptr;
  int w = 0;
  int h = 0;
  int pitch = 0;
  int bpp = 0;
  const VideoColor* palette = nullptr;

  constexpr VideoSurfaceView() noexcept = default;
  constexpr VideoSurfaceView(void* p, int width, int height, int pt,
                             int bytes_per_pixel,
                             const VideoColor* pal = nullptr) noexcept
      : pixels(static_cast<uint8_t*>(p)),
        w(width),
        h(height),
        pitch(pt),
        bpp(bytes_per_pixel),
        palette(pal) {}
  VideoSurfaceView(std::nullptr_t) noexcept {}
  VideoSurfaceView(const VideoSurface* s) noexcept;
  VideoSurfaceView(const VideoSurface& s) noexcept;
  explicit VideoSurfaceView(const VideoSurfaceView* v) noexcept {
    if (v != nullptr) {
      *this = *v;
    }
  }
  explicit VideoSurfaceView(VideoSurfaceView* v) noexcept {
    if (v != nullptr) {
      *this = *v;
    }
  }
};

struct VideoSurface {
  std::vector<uint8_t> pixel_data;
  uint8_t* pixels = nullptr;
  int w = 0;
  int h = 0;
  int pitch = 0;
  int bpp = 0;
  std::array<VideoColor, video_palette_size> palette{};
};

inline VideoSurfaceView::VideoSurfaceView(const VideoSurface* s) noexcept {
  if (s != nullptr) {
    pixels = s->pixels;
    w = s->w;
    h = s->h;
    pitch = s->pitch;
    bpp = s->bpp;
    palette = s->palette.data();
  }
}

inline VideoSurfaceView::VideoSurfaceView(const VideoSurface& s) noexcept
    : VideoSurfaceView(&s) {}

inline auto video_surface_view(const VideoSurface* s) noexcept
    -> VideoSurfaceView {
  if (s == nullptr) {
    return {};
  }
  return {s->pixels, s->w, s->h, s->pitch, s->bpp, s->palette.data()};
}

auto video_create_surface(int w, int h, int bpp) -> VideoSurface*;
auto video_destroy_surface(VideoSurface* s) -> void;
auto video_load_xpm(const char* const* xpm) -> VideoSurface*;
