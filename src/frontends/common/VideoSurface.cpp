// SPDX-License-Identifier: GPL-2.0-only

#include "frontends/common/VideoSurface.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

auto video_create_surface(int w, int h, int bpp) -> VideoSurface_t* {
  if (w <= 0 || h <= 0 || bpp <= 0) return nullptr;
  auto* s = static_cast<VideoSurface_t*>(calloc(1, sizeof(VideoSurface_t)));
  if (!s) return nullptr;
  s->w = w;
  s->h = h;
  s->bpp = bpp;
  s->pitch = w * bpp;
  size_t total_bytes = static_cast<size_t>(s->pitch) * static_cast<size_t>(h);
  s->pixels = static_cast<uint8_t*>(calloc(1, total_bytes));
  if (!s->pixels) {
    free(s);
    return nullptr;
  }
  return s;
}

auto video_destroy_surface(VideoSurface_t* s) -> void {
  if (s) {
    free(s->pixels);
    free(s);
  }
}

static auto hex_to_int(char c) -> uint8_t {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

auto video_load_xpm(const char* const* xpm) -> VideoSurface_t* {
  if (!xpm || !xpm[0]) return nullptr;
  int w = 0, h = 0, colors = 0, cpp = 0;
  if (sscanf(xpm[0], "%d %d %d %d", &w, &h, &colors, &cpp) != 4) return nullptr;
  if (cpp != 1 || colors < 0 || colors > 256 || w <= 0 || h <= 0)
    return nullptr;

  VideoSurface_t* s = video_create_surface(w, h, 1);
  if (!s) return nullptr;
  struct {
    char c;
    VideoColor_t color;
  } palette_map[256];
  for (int i = 0; i < colors; ++i) {
    if (!xpm[1 + i]) {
      video_destroy_surface(s);
      return nullptr;
    }
    char c = 0;
    char color_str[32] = {0};
    if (sscanf(xpm[1 + i], "%c c %31s", &c, color_str) != 2) {
      video_destroy_surface(s);
      return nullptr;
    }
    palette_map[i].c = c;
    if (color_str[0] == '#' && strlen(color_str) >= 7) {
      uint8_t r = (hex_to_int(color_str[1]) << 4) | hex_to_int(color_str[2]);
      uint8_t g = (hex_to_int(color_str[3]) << 4) | hex_to_int(color_str[4]);
      uint8_t b = (hex_to_int(color_str[5]) << 4) | hex_to_int(color_str[6]);
      palette_map[i].color = {r, g, b, 255};
    } else if (strcmp(color_str, "None") == 0) {
      palette_map[i].color = {0, 0, 0, 0};
    } else {
      palette_map[i].color = {0, 0, 0, 255};
    }
    s->palette[i] = palette_map[i].color;
  }

  for (int y = 0; y < h; ++y) {
    const char* line = xpm[1 + colors + y];
    if (!line) continue;
    for (int x = 0; x < w; ++x) {
      if (line[x] == '\0') break;
      char c = line[x];
      uint8_t color_idx = 0;
      for (int i = 0; i < colors; ++i) {
        if (palette_map[i].c == c) {
          color_idx = static_cast<uint8_t>(i);
          break;
        }
      }
      s->pixels[y * s->pitch + x] = color_idx;
    }
  }

  return s;
}
