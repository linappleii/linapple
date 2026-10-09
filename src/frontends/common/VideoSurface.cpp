// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/VideoSurface.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "core/Util_Text.h"

auto video_create_surface(int w, int h, int bpp) -> VideoSurface* {
  if (w <= 0 || h <= 0 || bpp <= 0) {
    return nullptr;
  }
  auto s = std::unique_ptr<VideoSurface>{new VideoSurface{}};
  s->w = w;
  s->h = h;
  s->bpp = bpp;
  s->pitch = w * bpp;
  const size_t total_bytes =
      static_cast<size_t>(s->pitch) * static_cast<size_t>(h);
  s->pixel_data.resize(total_bytes, 0);
  s->pixels = s->pixel_data.data();
  return s.release();
}

auto video_destroy_surface(VideoSurface* s) -> void { delete s; }

namespace {

constexpr size_t color_str_size = 32;
constexpr size_t hex_color_min_len = 7;
constexpr uint8_t opaque_alpha = 255;

struct PaletteMapEntry {
  char c = 0;
  VideoColor color{};
};

}  // namespace

auto video_load_xpm(const char* const* xpm) -> VideoSurface* {
  if (xpm == nullptr || xpm[0] == nullptr) {
    return nullptr;
  }
  int w = 0;
  int h = 0;
  int colors = 0;
  int cpp = 0;
  char* end = nullptr;
  const char* p = xpm[0];
  w = static_cast<int>(strtol(p, &end, 10));
  if (end == p) {
    return nullptr;
  }
  p = end;
  h = static_cast<int>(strtol(p, &end, 10));
  if (end == p) {
    return nullptr;
  }
  p = end;
  colors = static_cast<int>(strtol(p, &end, 10));
  if (end == p) {
    return nullptr;
  }
  p = end;
  cpp = static_cast<int>(strtol(p, &end, 10));
  if (end == p) {
    return nullptr;
  }
  if (cpp != 1 || colors < 0 || colors > static_cast<int>(video_palette_size) ||
      w <= 0 || h <= 0) {
    return nullptr;
  }

  VideoSurface* s = video_create_surface(w, h, 1);
  if (s == nullptr) {
    return nullptr;
  }

  std::array<PaletteMapEntry, video_palette_size> palette_map{};
  for (int i = 0; i < colors; ++i) {
    if (xpm[1 + i] == nullptr) {
      video_destroy_surface(s);
      return nullptr;
    }
    char c = 0;
    char color_str[color_str_size] = {0};
    if (sscanf(xpm[1 + i], "%c c %31s", &c, color_str) != 2) {
      video_destroy_surface(s);
      return nullptr;
    }
    palette_map.at(static_cast<size_t>(i)).c = c;
    if (color_str[0] == '#' && strlen(color_str) >= hex_color_min_len) {
      const uint8_t r = text_convert_2_chars_to_byte(&color_str[1]);
      const uint8_t g = text_convert_2_chars_to_byte(&color_str[3]);
      const uint8_t b = text_convert_2_chars_to_byte(&color_str[5]);
      palette_map.at(static_cast<size_t>(i)).color =
          VideoColor{r, g, b, opaque_alpha};
    } else if (strcmp(color_str, "None") == 0) {
      palette_map.at(static_cast<size_t>(i)).color = VideoColor{0, 0, 0, 0};
    } else {
      palette_map.at(static_cast<size_t>(i)).color =
          VideoColor{0, 0, 0, opaque_alpha};
    }
    s->palette.at(static_cast<size_t>(i)) =
        palette_map.at(static_cast<size_t>(i)).color;
  }

  for (int y = 0; y < h; ++y) {
    const char* line = xpm[1 + colors + y];
    if (line == nullptr) {
      continue;
    }
    for (int x = 0; x < w; ++x) {
      if (line[x] == '\0') {
        break;
      }
      const char c = line[x];
      uint8_t color_idx = 0;
      for (int i = 0; i < colors; ++i) {
        if (palette_map.at(static_cast<size_t>(i)).c == c) {
          color_idx = static_cast<uint8_t>(i);
          break;
        }
      }
      s->pixels[(y * s->pitch) + x] = color_idx;
    }
  }

  return s;
}
