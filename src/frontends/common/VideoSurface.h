// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

struct VideoColor_t {
  uint8_t r, g, b, a;
};
using VideoColor_t = VideoColor_t;

struct VideoRect_t {
  int x, y, w, h;
};
using VideoRect_t = VideoRect_t;

constexpr size_t VIDEO_PALETTE_SIZE = 256;

struct VideoSurface_t {
  uint8_t* pixels;
  int w, h, pitch;
  int bpp;
  VideoColor_t palette[VIDEO_PALETTE_SIZE];
};
using VideoSurface_t = VideoSurface_t;

auto video_create_surface(int w, int h, int bpp) -> VideoSurface_t*;
auto video_destroy_surface(VideoSurface_t* s) -> void;
auto video_load_xpm(const char* const* xpm) -> VideoSurface_t*;
