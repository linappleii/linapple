// SPDX-License-Identifier: GPL-2.0-only

#include "frontends/common/VideoStretch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "frontends/common/VideoSurface.h"

template <typename T>
static auto copy_row(T* src, int src_w, T* dst, int dst_x, int dst_w, int max_w)
    -> void {
  if (dst_w <= 0 || src_w <= 0 || !src || !dst) {
    return;
  }
  if (dst_x >= max_w || dst_x + dst_w <= 0) {
    return;
  }
  if (src_w == dst_w) {
    for (int i = 0; i < dst_w; ++i) {
      int cur_x = dst_x + i;
      if (cur_x >= 0 && cur_x < max_w) {
        dst[cur_x] = src[i];
      }
    }
    return;
  }
  for (int i = 0; i < dst_w; ++i) {
    int src_x = static_cast<int>((static_cast<int64_t>(i) * src_w) / dst_w);
    if (src_x >= src_w) {
      src_x = src_w - 1;
    }
    int cur_x = dst_x + i;
    if (cur_x >= 0 && cur_x < max_w) {
      dst[cur_x] = src[src_x];
    }
  }
}

template <typename T>
static auto copy_row_or(T* src, int src_w, T* dst, int dst_x, int dst_w,
                        int max_w) -> void {
  if (dst_w <= 0 || src_w <= 0 || !src || !dst) {
    return;
  }
  if (dst_x >= max_w || dst_x + dst_w <= 0) {
    return;
  }
  if (src_w == dst_w) {
    for (int i = 0; i < dst_w; ++i) {
      int cur_x = dst_x + i;
      if (cur_x >= 0 && cur_x < max_w) {
        dst[cur_x] |= src[i];
      }
    }
    return;
  }
  for (int i = 0; i < dst_w; ++i) {
    int src_x = static_cast<int>((static_cast<int64_t>(i) * src_w) / dst_w);
    if (src_x >= src_w) {
      src_x = src_w - 1;
    }
    int cur_x = dst_x + i;
    if (cur_x >= 0 && cur_x < max_w) {
      dst[cur_x] |= src[src_x];
    }
  }
}

static auto copy_row1(uint8_t* src, int src_w, uint8_t* dst, int dst_x,
                      int dst_w, int max_w) -> void {
  copy_row(src, src_w, dst, dst_x, dst_w, max_w);
}
static auto copy_row2(uint16_t* src, int src_w, uint16_t* dst, int dst_x,
                      int dst_w, int max_w) -> void {
  copy_row(src, src_w, dst, dst_x, dst_w, max_w);
}
static auto copy_row4(uint32_t* src, int src_w, uint32_t* dst, int dst_x,
                      int dst_w, int max_w) -> void {
  copy_row(src, src_w, dst, dst_x, dst_w, max_w);
}
static auto copy_row_or1(uint8_t* src, int src_w, uint8_t* dst, int dst_x,
                         int dst_w, int max_w) -> void {
  copy_row_or(src, src_w, dst, dst_x, dst_w, max_w);
}
static auto copy_row_or2(uint16_t* src, int src_w, uint16_t* dst, int dst_x,
                         int dst_w, int max_w) -> void {
  copy_row_or(src, src_w, dst, dst_x, dst_w, max_w);
}
static auto copy_row_or4(uint32_t* src, int src_w, uint32_t* dst, int dst_x,
                         int dst_w, int max_w) -> void {
  copy_row_or(src, src_w, dst, dst_x, dst_w, max_w);
}

static uint32_t g_palette_lut[256] = {};
static const VideoColor_t* g_last_palette = nullptr;

static auto update_palette_lut(const VideoColor_t* palette) -> void {
  if (!palette) {
    return;
  }
  if (palette == g_last_palette) {
    return;
  }

  for (int i = 0; i < 256; ++i) {
    g_palette_lut[i] =
        (palette[i].r << 16) | (palette[i].g << 8) | palette[i].b;
  }
  g_last_palette = palette;
}

static auto copy_row1to4(uint8_t* src, int src_w, uint32_t* dst, int dst_x,
                         int dst_w, int max_w, const VideoColor_t* palette)
    -> void {
  if (dst_w <= 0 || src_w <= 0 || !src || !dst) {
    return;
  }
  if (dst_x >= max_w || dst_x + dst_w <= 0) {
    return;
  }
  update_palette_lut(palette);
  if (src_w == dst_w) {
    for (int i = 0; i < dst_w; ++i) {
      int cur_x = dst_x + i;
      if (cur_x >= 0 && cur_x < max_w) {
        dst[cur_x] = g_palette_lut[src[i]];
      }
    }
    return;
  }
  for (int i = 0; i < dst_w; ++i) {
    int src_x = static_cast<int>((static_cast<int64_t>(i) * src_w) / dst_w);
    if (src_x >= src_w) {
      src_x = src_w - 1;
    }
    int cur_x = dst_x + i;
    if (cur_x >= 0 && cur_x < max_w) {
      dst[cur_x] = g_palette_lut[src[src_x]];
    }
  }
}

static auto copy_row_or1to4(uint8_t* src, int src_w, uint32_t* dst, int dst_x,
                            int dst_w, int max_w, const VideoColor_t* palette)
    -> void {
  if (dst_w <= 0 || src_w <= 0 || !src || !dst) {
    return;
  }
  if (dst_x >= max_w || dst_x + dst_w <= 0) {
    return;
  }
  update_palette_lut(palette);
  if (src_w == dst_w) {
    for (int i = 0; i < dst_w; ++i) {
      int cur_x = dst_x + i;
      if (cur_x >= 0 && cur_x < max_w) {
        dst[cur_x] |= g_palette_lut[src[i]];
      }
    }
    return;
  }
  for (int i = 0; i < dst_w; ++i) {
    int src_x = static_cast<int>((static_cast<int64_t>(i) * src_w) / dst_w);
    if (src_x >= src_w) {
      src_x = src_w - 1;
    }
    int cur_x = dst_x + i;
    if (cur_x >= 0 && cur_x < max_w) {
      dst[cur_x] |= g_palette_lut[src[src_x]];
    }
  }
}

static auto copy_row3(uint8_t* src, int src_w, uint8_t* dst, int dst_x,
                      int dst_w, int max_w) -> void {
  if (dst_w <= 0 || src_w <= 0 || !src || !dst) {
    return;
  }
  if (dst_x >= max_w || dst_x + dst_w <= 0) {
    return;
  }
  for (int i = 0; i < dst_w; ++i) {
    int src_x = static_cast<int>((static_cast<int64_t>(i) * src_w) / dst_w);
    if (src_x >= src_w) {
      src_x = src_w - 1;
    }
    int cur_x = dst_x + i;
    if (cur_x >= 0 && cur_x < max_w) {
      dst[cur_x * 3] = src[src_x * 3];
      dst[cur_x * 3 + 1] = src[src_x * 3 + 1];
      dst[cur_x * 3 + 2] = src[src_x * 3 + 2];
    }
  }
}

static auto copy_row3to4(uint8_t* src, int src_w, uint32_t* dst, int dst_x,
                         int dst_w, int max_w) -> void {
  if (dst_w <= 0 || src_w <= 0 || !src || !dst) {
    return;
  }
  if (dst_x >= max_w || dst_x + dst_w <= 0) {
    return;
  }
  for (int i = 0; i < dst_w; ++i) {
    int src_x = static_cast<int>((static_cast<int64_t>(i) * src_w) / dst_w);
    if (src_x >= src_w) {
      src_x = src_w - 1;
    }
    int cur_x = dst_x + i;
    if (cur_x >= 0 && cur_x < max_w) {
      uint32_t r = src[src_x * 3];
      uint32_t g = src[src_x * 3 + 1];
      uint32_t b = src[src_x * 3 + 2];
      dst[cur_x] = (r << 16) | (g << 8) | b;
    }
  }
}

static auto copy_row_or3to4(uint8_t* src, int src_w, uint32_t* dst, int dst_x,
                            int dst_w, int max_w) -> void {
  if (dst_w <= 0 || src_w <= 0 || !src || !dst) {
    return;
  }
  if (dst_x >= max_w || dst_x + dst_w <= 0) {
    return;
  }
  for (int i = 0; i < dst_w; ++i) {
    int src_x = static_cast<int>((static_cast<int64_t>(i) * src_w) / dst_w);
    if (src_x >= src_w) {
      src_x = src_w - 1;
    }
    int cur_x = dst_x + i;
    if (cur_x >= 0 && cur_x < max_w) {
      uint32_t r = src[src_x * 3];
      uint32_t g = src[src_x * 3 + 1];
      uint32_t b = src[src_x * 3 + 2];
      dst[cur_x] |= (r << 16) | (g << 8) | b;
    }
  }
}

static auto copy8mono(uint8_t* src, int src_w, uint8_t* dst, int dst_x,
                      int dst_w, int max_w, uint8_t fgbrush, uint8_t bgbrush)
    -> void {
  if (dst_w <= 0 || src_w <= 0 || !src || !dst) {
    return;
  }
  if (dst_x >= max_w || dst_x + dst_w <= 0) {
    return;
  }
  for (int i = 0; i < dst_w; ++i) {
    int src_x = static_cast<int>((static_cast<int64_t>(i) * src_w) / dst_w);
    if (src_x >= src_w) {
      src_x = src_w - 1;
    }
    int cur_x = dst_x + i;
    if (cur_x >= 0 && cur_x < max_w) {
      dst[cur_x] = src[src_x] ? fgbrush : bgbrush;
    }
  }
}

static auto copy8mono4(uint8_t* src, int src_w, uint32_t* dst, int dst_x,
                       int dst_w, int max_w, uint32_t fgbrush, uint32_t bgbrush)
    -> void {
  if (dst_w <= 0 || src_w <= 0 || !src || !dst) {
    return;
  }
  if (dst_x >= max_w || dst_x + dst_w <= 0) {
    return;
  }
  for (int i = 0; i < dst_w; ++i) {
    int src_x = static_cast<int>((static_cast<int64_t>(i) * src_w) / dst_w);
    if (src_x >= src_w) {
      src_x = src_w - 1;
    }
    int cur_x = dst_x + i;
    if (cur_x >= 0 && cur_x < max_w) {
      dst[cur_x] = src[src_x] ? fgbrush : bgbrush;
    }
  }
}

static inline auto clip_source_rect(VideoSurfaceView_t src, VideoRect_t* rect)
    -> bool {
  if (src.pixels == nullptr || rect == nullptr) {
    return false;
  }
  if (rect->x + rect->w <= 0 || rect->y + rect->h <= 0) {
    return false;
  }
  if (rect->x < 0) {
    rect->w += rect->x;
    rect->x = 0;
  }
  if (rect->y < 0) {
    rect->h += rect->y;
    rect->y = 0;
  }
  if (rect->x >= src.w || rect->y >= src.h) {
    return false;
  }
  if (rect->x + rect->w > src.w) {
    rect->w = src.w - rect->x;
  }
  if (rect->y + rect->h > src.h) {
    rect->h = src.h - rect->y;
  }
  return (rect->w > 0 && rect->h > 0);
}

template <typename RowOp>
static auto video_soft_stretch_impl(VideoSurfaceView_t src,
                                    const VideoRect_t* srcrect,
                                    VideoSurfaceView_t dst,
                                    const VideoRect_t* dstrect, RowOp row_op)
    -> int {
  if (!src.pixels || !dst.pixels) {
    return -1;
  }

  VideoRect_t full_src = srcrect ? *srcrect : VideoRect_t{0, 0, src.w, src.h};
  VideoRect_t full_dst = dstrect ? *dstrect : VideoRect_t{0, 0, dst.w, dst.h};

  if (full_dst.h <= 0 || full_src.h <= 0 || full_dst.w <= 0 ||
      full_src.w <= 0) {
    return -1;
  }

  if (!clip_source_rect(src, &full_src)) {
    return 0;
  }

  const int sbpp = src.bpp;
  for (int row_idx = 0; row_idx < full_dst.h; ++row_idx) {
    int cur_dst_row = full_dst.y + row_idx;
    if (cur_dst_row < 0 || cur_dst_row >= dst.h) {
      continue;
    }

    int cur_src_row =
        full_src.y +
        static_cast<int>((static_cast<int64_t>(row_idx) * full_src.h) /
                         full_dst.h);
    if (cur_src_row < 0 || cur_src_row >= src.h) {
      continue;
    }

    uint8_t* srcp = src.pixels + (cur_src_row * src.pitch) +
                    (static_cast<ptrdiff_t>(full_src.x * sbpp));
    uint8_t* dst_row_base = dst.pixels + (cur_dst_row * dst.pitch);

    row_op(srcp, full_src.w, dst_row_base, full_dst.x, full_dst.w, dst.w);
  }

  return 0;
}

auto video_soft_stretch(VideoSurfaceView_t src, const VideoRect_t* srcrect,
                        VideoSurfaceView_t dst, const VideoRect_t* dstrect)
    -> int {
  return video_soft_stretch_impl(
      src, srcrect, dst, dstrect,
      [&](uint8_t* srcp, int src_w, uint8_t* dst_row_base, int dst_x, int dst_w,
          int max_w) {
        switch (dst.bpp) {
          case 1:
            copy_row1(srcp, src_w, dst_row_base, dst_x, dst_w, max_w);
            break;
          case 2:
            copy_row2(reinterpret_cast<uint16_t*>(srcp), src_w,
                      reinterpret_cast<uint16_t*>(dst_row_base), dst_x, dst_w,
                      max_w);
            break;
          case 3:
            copy_row3(srcp, src_w, dst_row_base, dst_x, dst_w, max_w);
            break;
          case 4: {
            auto* dst32 = reinterpret_cast<uint32_t*>(dst_row_base);
            switch (src.bpp) {
              case 1:
                copy_row1to4(srcp, src_w, dst32, dst_x, dst_w, max_w,
                             src.palette);
                break;
              case 3:
                copy_row3to4(srcp, src_w, dst32, dst_x, dst_w, max_w);
                break;
              case 4:
                copy_row4(reinterpret_cast<uint32_t*>(srcp), src_w, dst32,
                          dst_x, dst_w, max_w);
                break;
              default:
                break;
            }
            break;
          }
          default:
            break;
        }
      });
}

auto video_soft_stretch_mono8(VideoSurfaceView_t src,
                              const VideoRect_t* srcrect,
                              VideoSurfaceView_t dst,
                              const VideoRect_t* dstrect, uint32_t fgbrush,
                              uint32_t bgbrush) -> int {
  return video_soft_stretch_impl(
      src, srcrect, dst, dstrect,
      [&](uint8_t* srcp, int src_w, uint8_t* dst_row_base, int dst_x, int dst_w,
          int max_w) {
        if (src.bpp == 1 && dst.bpp == 4) {
          copy8mono4(srcp, src_w, reinterpret_cast<uint32_t*>(dst_row_base),
                     dst_x, dst_w, max_w, fgbrush, bgbrush);
        } else if (dst.bpp == 1) {
          copy8mono(srcp, src_w, dst_row_base, dst_x, dst_w, max_w,
                    static_cast<uint8_t>(fgbrush),
                    static_cast<uint8_t>(bgbrush));
        }
      });
}

auto video_soft_stretch_or(VideoSurfaceView_t src, const VideoRect_t* srcrect,
                           VideoSurfaceView_t dst, const VideoRect_t* dstrect)
    -> int {
  return video_soft_stretch_impl(
      src, srcrect, dst, dstrect,
      [&](uint8_t* srcp, int src_w, uint8_t* dst_row_base, int dst_x, int dst_w,
          int max_w) {
        switch (dst.bpp) {
          case 1:
            copy_row_or1(srcp, src_w, dst_row_base, dst_x, dst_w, max_w);
            break;
          case 2:
            copy_row_or2(reinterpret_cast<uint16_t*>(srcp), src_w,
                         reinterpret_cast<uint16_t*>(dst_row_base), dst_x,
                         dst_w, max_w);
            break;
          case 3:
            copy_row3(srcp, src_w, dst_row_base, dst_x, dst_w, max_w);
            break;
          case 4: {
            auto* dst32 = reinterpret_cast<uint32_t*>(dst_row_base);
            switch (src.bpp) {
              case 1:
                copy_row_or1to4(srcp, src_w, dst32, dst_x, dst_w, max_w,
                                src.palette);
                break;
              case 3:
                copy_row_or3to4(srcp, src_w, dst32, dst_x, dst_w, max_w);
                break;
              case 4:
                copy_row_or4(reinterpret_cast<uint32_t*>(srcp), src_w, dst32,
                             dst_x, dst_w, max_w);
                break;
              default:
                break;
            }
            break;
          }
          default:
            break;
        }
      });
}

VideoSurface_t* font_sfc = nullptr;

auto fonts_initialization() -> bool { return true; }

auto fonts_termination() -> void {
  if (font_sfc) {
    free(font_sfc->pixels);
    free(font_sfc);
    font_sfc = nullptr;
  }
}

auto font_print(int x, int y, const char* text, VideoSurfaceView_t surface,
                double kx, double ky) -> void {
  int i = 0, c = 0;
  VideoRect_t s{}, d{};

  if (font_sfc == nullptr || text == nullptr || surface.pixels == nullptr ||
      y >= surface.h) {
    return;
  }

  for (i = 0; text[i] != 0; i++) {
    int row = 0;
    c = static_cast<uint8_t>(text[i]);

    if (c > 127) {
      c = '?';
    }

    row = c / k_chars_in_row;

    s.x = (c - (row * k_chars_in_row)) * (k_font_size_x + 1) + 1;
    s.y = (row) * (k_font_size_y + 1) + 1;
    s.h = k_font_size_y;
    s.w = k_font_size_x;

    d.x = static_cast<int>(x + i * k_font_size_x * kx);
    d.y = y;
    d.w = static_cast<int>(s.w * kx);
    d.h = static_cast<int>(s.h * ky);

    if (d.x >= surface.w) {
      break;
    }
    if (d.x + d.w <= 0 || d.y + d.h <= 0) {
      continue;
    }
    video_soft_stretch_or(font_sfc, &s, surface, &d);
  }
}

auto font_print_right(int x, int y, const char* text,
                      VideoSurfaceView_t surface, double kx, double ky)
    -> void {
  if (text == nullptr) {
    return;
  }
  const auto offset = static_cast<int>(strlen(text) * k_font_size_x * kx);
  font_print(x - offset, y, text, surface, kx, ky);
}

auto font_print_centered(int x, int y, const char* text,
                         VideoSurfaceView_t surface, double kx, double ky)
    -> void {
  if (text == nullptr) {
    return;
  }
  const auto offset = static_cast<int>(strlen(text) * k_font_size_x * kx / 2.0);
  font_print(x - offset, y, text, surface, kx, ky);
}

auto surface_fader(VideoSurface_t* surface, float r_factor, float g_factor,
                   float b_factor, float a_factor, const VideoRect_t* r)
    -> void {
  (void)a_factor;
  (void)r;
  int i = 0;
  VideoColor_t* colors = nullptr;

  if (!surface || surface->bpp != 1) {
    return;
  }

  colors = surface->palette.data();
  for (i = 0; i < 256; i++) {
    colors[i].r = static_cast<uint8_t>(colors[i].r * r_factor);
    colors[i].g = static_cast<uint8_t>(colors[i].g * g_factor);
    colors[i].b = static_cast<uint8_t>(colors[i].b * b_factor);
  }
}

auto putpixel(VideoSurfaceView_t surface, int x, int y, uint32_t pixel)
    -> void {
  if (surface.pixels == nullptr || x < 0 || x >= surface.w || y < 0 ||
      y >= surface.h) {
    return;
  }

  uint8_t* p = surface.pixels + y * surface.pitch +
               static_cast<ptrdiff_t>(x * surface.bpp);

  switch (surface.bpp) {
    case 1:
      *p = static_cast<uint8_t>(pixel);
      break;
    case 2:
      *reinterpret_cast<uint16_t*>(p) = static_cast<uint16_t>(pixel);
      break;
    case 4:
      *reinterpret_cast<uint32_t*>(p) = pixel;
      break;
    default:
      break;
  }
}

auto rectangle(VideoSurfaceView_t surface, int x, int y, int w, int h,
               uint32_t pixel) -> void {
  if (surface.pixels == nullptr) {
    return;
  }
  int i = 0;

  for (i = 0; i < w; i++) {
    putpixel(surface, x + i, y, pixel);
    putpixel(surface, x + i, y + h, pixel);
  }
  for (i = 0; i <= h; i++) {
    putpixel(surface, x, y + i, pixel);
    putpixel(surface, x + w, y + i, pixel);
  }
}

auto fill_rectangle(VideoSurfaceView_t surface, int x, int y, int w, int h,
                    uint32_t pixel) -> void {
  if (surface.pixels == nullptr || w <= 0 || h <= 0) {
    return;
  }
  const int x_start = std::max(0, x);
  const int y_start = std::max(0, y);
  const int x_end = std::min(surface.w, x + w);
  const int y_end = std::min(surface.h, y + h);

  for (int cy = y_start; cy < y_end; ++cy) {
    for (int cx = x_start; cx < x_end; ++cx) {
      putpixel(surface, cx, cy, pixel);
    }
  }
}
