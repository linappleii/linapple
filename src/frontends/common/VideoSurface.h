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

struct VideoSurface_t {
  std::vector<uint8_t> pixel_data{};
  uint8_t* pixels = nullptr;
  int w = 0;
  int h = 0;
  int pitch = 0;
  int bpp = 0;
  std::array<VideoColor_t, k_video_palette_size> palette{};
};

auto video_create_surface(int w, int h, int bpp) -> VideoSurface_t*;
auto video_destroy_surface(VideoSurface_t* s) -> void;
auto video_load_xpm(const char* const* xpm) -> VideoSurface_t*;
