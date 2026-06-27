// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

struct VideoColor_t {
  uint8_t r, g, b, a;
};
using VideoColor = VideoColor_t;

struct VideoRect_t {
  int x, y, w, h;
};
using VideoRect = VideoRect_t;

struct VideoSurface_t {
  uint8_t* pixels;
  int w, h, pitch;
  int bpp;
  VideoColor_t palette[256];
};
using VideoSurface = VideoSurface_t;

auto video_create_surface(int w, int h, int bpp) -> VideoSurface_t*;
auto video_destroy_surface(VideoSurface_t* s) -> void;
auto video_load_xpm(const char* const* xpm) -> VideoSurface_t*;

// Legacy wrappers for compatibility
inline auto VideoCreateSurface(int w, int h, int bpp) -> VideoSurface_t* {
  return video_create_surface(w, h, bpp);
}
inline auto VideoDestroySurface(VideoSurface_t* s) -> void {
  video_destroy_surface(s);
}
inline auto VideoLoadXPM(const char* const* xpm) -> VideoSurface_t* {
  return video_load_xpm(xpm);
}
