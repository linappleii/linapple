// SPDX-License-Identifier: GPL-2.0-only

#include "frontends/common/VideoStretch.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic):
// Low-level pixel stretching and scanline blitting routines operating on raw
// framebuffers.

template <typename T>
static auto copy_row(T* src, int src_w, T* dst, int dst_w) -> void {
  if (dst_w <= 0 || src_w <= 0) return;
  int pos = 0x10000;
  int inc = (src_w << 16) / dst_w;
  T pixel = 0;
  for (int i = dst_w; i > 0; --i) {
    while (pos >= 0x10000L) {
      pixel = *src++;
      pos -= 0x10000L;
    }
    *dst++ = pixel;
    pos += inc;
  }
}

template <typename T>
static auto copy_row_or(T* src, int src_w, T* dst, int dst_w) -> void {
  if (dst_w <= 0 || src_w <= 0) return;
  int pos = 0x10000;
  int inc = (src_w << 16) / dst_w;
  T pixel = 0;
  for (int i = dst_w; i > 0; --i) {
    while (pos >= 0x10000L) {
      pixel = *src++;
      pos -= 0x10000L;
    }
    *dst++ |= pixel;
    pos += inc;
  }
}

static auto copy_row1(uint8_t* src, int src_w, uint8_t* dst, int dst_w)
    -> void {
  copy_row(src, src_w, dst, dst_w);
}
static auto copy_row2(uint16_t* src, int src_w, uint16_t* dst, int dst_w)
    -> void {
  copy_row(src, src_w, dst, dst_w);
}
static auto copy_row4(uint32_t* src, int src_w, uint32_t* dst, int dst_w)
    -> void {
  copy_row(src, src_w, dst, dst_w);
}
static auto copy_row_or1(uint8_t* src, int src_w, uint8_t* dst, int dst_w)
    -> void {
  copy_row_or(src, src_w, dst, dst_w);
}
static auto copy_row_or2(uint16_t* src, int src_w, uint16_t* dst, int dst_w)
    -> void {
  copy_row_or(src, src_w, dst, dst_w);
}
static auto copy_row_or4(uint32_t* src, int src_w, uint32_t* dst, int dst_w)
    -> void {
  copy_row_or(src, src_w, dst, dst_w);
}

static uint32_t g_palette_lut[256] = {};
static VideoColor_t* g_last_palette = nullptr;

static auto update_palette_lut(VideoColor_t* palette) -> void {
  if (!palette) return;
  if (palette == g_last_palette) return;

  for (int i = 0; i < 256; ++i) {
    g_palette_lut[i] =
        (palette[i].r << 16) | (palette[i].g << 8) | palette[i].b;
  }
  g_last_palette = palette;
}

static auto copy_row1to4(uint8_t* src, int src_w, uint32_t* dst, int dst_w,
                         VideoColor_t* palette) -> void {
  if (dst_w <= 0 || src_w <= 0) return;
  update_palette_lut(palette);
  if (src_w == dst_w) {
    for (int i = 0; i < dst_w; ++i) {
      *dst++ = g_palette_lut[*src++];
    }
    return;
  }
  int pos = 0x10000;
  int inc = (src_w << 16) / dst_w;
  uint32_t pixel = 0;
  for (int i = dst_w; i > 0; --i) {
    while (pos >= 0x10000L) {
      pixel = g_palette_lut[*src++];
      pos -= 0x10000L;
    }
    *dst++ = pixel;
    pos += inc;
  }
}

static auto copy_row_or1to4(uint8_t* src, int src_w, uint32_t* dst, int dst_w,
                            VideoColor_t* palette) -> void {
  if (dst_w <= 0 || src_w <= 0) return;
  update_palette_lut(palette);
  if (src_w == dst_w) {
    for (int i = 0; i < dst_w; ++i) {
      *dst++ |= g_palette_lut[*src++];
    }
    return;
  }
  int pos = 0x10000;
  int inc = (src_w << 16) / dst_w;
  uint32_t pixel = 0;
  for (int i = dst_w; i > 0; --i) {
    while (pos >= 0x10000L) {
      pixel = g_palette_lut[*src++];
      pos -= 0x10000L;
    }
    *dst++ |= pixel;
    pos += inc;
  }
}

static auto copy_row3(uint8_t* src, int src_w, uint8_t* dst, int dst_w)
    -> void {
  if (dst_w <= 0 || src_w <= 0) return;
  int i = 0;
  int pos = 0, inc = 0;
  uint8_t pixel[3] = {0, 0, 0};

  pos = 0x10000;
  inc = (src_w << 16) / dst_w;
  for (i = dst_w; i > 0; --i) {
    while (pos >= 0x10000L) {
      pixel[0] = *src++;
      pixel[1] = *src++;
      pixel[2] = *src++;
      pos -= 0x10000L;
    }
    *dst++ = pixel[0];
    *dst++ = pixel[1];
    *dst++ = pixel[2];
    pos += inc;
  }
}

auto video_soft_stretch(VideoSurface_t* src, VideoRect_t* srcrect,
                        VideoSurface_t* dst, VideoRect_t* dstrect) -> int {
  int pos = 0, inc = 0;
  int dst_maxrow = 0;
  int src_row = 0, dst_row = 0;
  uint8_t* srcp = nullptr;
  uint8_t* dstp = nullptr;
  VideoRect_t full_src{};
  VideoRect_t full_dst{};
  if (!src || !dst) return -1;
  if (!src->pixels || !dst->pixels) {
    return -1;
  }
  const int sbpp = src->bpp;
  const int dbpp = dst->bpp;

  if (!srcrect) {
    full_src.x = 0;
    full_src.y = 0;
    full_src.w = src->w;
    full_src.h = src->h;
    srcrect = &full_src;
  }
  if (!dstrect) {
    full_dst.x = 0;
    full_dst.y = 0;
    full_dst.w = dst->w;
    full_dst.h = dst->h;
    dstrect = &full_dst;
  }

  if (dstrect->h <= 0 || srcrect->h <= 0 || dstrect->w <= 0 ||
      srcrect->w <= 0) {
    return -1;
  }

  pos = 0x10000;
  inc = (srcrect->h << 16) / dstrect->h;
  src_row = srcrect->y;
  dst_row = dstrect->y;

  for (dst_maxrow = dst_row + dstrect->h; dst_row < dst_maxrow; ++dst_row) {
    dstp = dst->pixels + (dst_row * dst->pitch) +
           (static_cast<ptrdiff_t>(dstrect->x * dbpp));
    while (pos >= 0x10000L) {
      srcp = src->pixels + (src_row * src->pitch) +
             (static_cast<ptrdiff_t>(srcrect->x * sbpp));
      ++src_row;
      pos -= 0x10000L;
    }
    if (sbpp == 1 && dbpp == 4) {
      copy_row1to4(srcp, srcrect->w, reinterpret_cast<uint32_t*>(dstp),
                   dstrect->w, src->palette);
    } else {
      switch (dbpp) {
        case 1:
          copy_row1(srcp, srcrect->w, dstp, dstrect->w);
          break;
        case 2:
          copy_row2(reinterpret_cast<uint16_t*>(srcp), srcrect->w,
                    reinterpret_cast<uint16_t*>(dstp), dstrect->w);
          break;
        case 3:
          copy_row3(srcp, srcrect->w, dstp, dstrect->w);
          break;
        case 4:
          copy_row4(reinterpret_cast<uint32_t*>(srcp), srcrect->w,
                    reinterpret_cast<uint32_t*>(dstp), dstrect->w);
          break;
        default:
          break;
      }
    }
    pos += inc;
  }

  return 0;
}

static auto copy8mono(uint8_t* src, int src_w, uint8_t* dst, int dst_w,
                      uint8_t fgbrush, uint8_t bgbrush) -> void {
  if (dst_w <= 0 || src_w <= 0) return;
  int i = 0;
  int pos = 0, inc = 0;
  uint8_t pixel = 0;
  pos = 0x10000;
  inc = (src_w << 16) / dst_w;
  for (i = dst_w; i > 0; --i) {
    while (pos >= 0x10000L) {
      pixel = *src++;
      pos -= 0x10000L;
    }
    if (pixel) {
      *dst++ = fgbrush;
    } else {
      *dst++ = bgbrush;
    }
    pos += inc;
  }
}

static auto copy8mono4(uint8_t* src, int src_w, uint32_t* dst, int dst_w,
                       uint32_t fgbrush, uint32_t bgbrush) -> void {
  if (dst_w <= 0 || src_w <= 0) return;
  int i = 0;
  int pos = 0, inc = 0;
  uint8_t pixel = 0;
  pos = 0x10000;
  inc = (src_w << 16) / dst_w;
  for (i = dst_w; i > 0; --i) {
    while (pos >= 0x10000L) {
      pixel = *src++;
      pos -= 0x10000L;
    }
    if (pixel) {
      *dst++ = fgbrush;
    } else {
      *dst++ = bgbrush;
    }
    pos += inc;
  }
}

auto video_soft_stretch_mono8(VideoSurface_t* src, VideoRect_t* srcrect,
                              VideoSurface_t* dst, VideoRect_t* dstrect,
                              uint32_t fgbrush, uint32_t bgbrush) -> int {
  int pos = 0, inc = 0;
  int dst_maxrow = 0;
  int src_row = 0, dst_row = 0;
  uint8_t* srcp = nullptr;
  uint8_t* dstp = nullptr;
  VideoRect_t full_src{};
  VideoRect_t full_dst{};
  if (!src || !dst) return -1;
  if (!src->pixels || !dst->pixels) {
    return -1;
  }
  const int sbpp = src->bpp;
  const int dbpp = dst->bpp;

  if (!srcrect) {
    full_src.x = 0;
    full_src.y = 0;
    full_src.w = src->w;
    full_src.h = src->h;
    srcrect = &full_src;
  }
  if (!dstrect) {
    full_dst.x = 0;
    full_dst.y = 0;
    full_dst.w = dst->w;
    full_dst.h = dst->h;
    dstrect = &full_dst;
  }

  if (dstrect->h <= 0 || srcrect->h <= 0 || dstrect->w <= 0 ||
      srcrect->w <= 0) {
    return -1;
  }

  pos = 0x10000;
  inc = (srcrect->h << 16) / dstrect->h;
  src_row = srcrect->y;
  dst_row = dstrect->y;

  for (dst_maxrow = dst_row + dstrect->h; dst_row < dst_maxrow; ++dst_row) {
    dstp = dst->pixels + (dst_row * dst->pitch) +
           (static_cast<ptrdiff_t>(dstrect->x * dbpp));
    while (pos >= 0x10000L) {
      srcp = src->pixels + (src_row * src->pitch) +
             (static_cast<ptrdiff_t>(srcrect->x * sbpp));
      ++src_row;
      pos -= 0x10000L;
    }
    if (sbpp == 1 && dbpp == 4) {
      copy8mono4(srcp, srcrect->w, reinterpret_cast<uint32_t*>(dstp),
                 dstrect->w, fgbrush, bgbrush);
    } else {
      switch (dbpp) {
        case 1:
          copy8mono(srcp, srcrect->w, dstp, dstrect->w,
                    static_cast<uint8_t>(fgbrush),
                    static_cast<uint8_t>(bgbrush));
          break;
        default:
          break;
      }
    }
    pos += inc;
  }

  return 0;
}

auto video_soft_stretch_or(VideoSurface_t* src, VideoRect_t* srcrect,
                           VideoSurface_t* dst, VideoRect_t* dstrect) -> int {
  int pos = 0, inc = 0;
  int dst_maxrow = 0;
  int src_row = 0, dst_row = 0;
  uint8_t* srcp = nullptr;
  uint8_t* dstp = nullptr;
  VideoRect_t full_src{};
  VideoRect_t full_dst{};
  if (!src || !dst) return -1;
  if (!src->pixels || !dst->pixels) {
    return -1;
  }
  const int sbpp = src->bpp;
  const int dbpp = dst->bpp;

  if (!srcrect) {
    full_src.x = 0;
    full_src.y = 0;
    full_src.w = src->w;
    full_src.h = src->h;
    srcrect = &full_src;
  }
  if (!dstrect) {
    full_dst.x = 0;
    full_dst.y = 0;
    full_dst.w = dst->w;
    full_dst.h = dst->h;
    dstrect = &full_dst;
  }

  if (dstrect->h <= 0 || srcrect->h <= 0 || dstrect->w <= 0 ||
      srcrect->w <= 0) {
    return -1;
  }

  pos = 0x10000;
  inc = (srcrect->h << 16) / dstrect->h;
  src_row = srcrect->y;
  dst_row = dstrect->y;

  for (dst_maxrow = dst_row + dstrect->h; dst_row < dst_maxrow; ++dst_row) {
    dstp = dst->pixels + (dst_row * dst->pitch) +
           (static_cast<ptrdiff_t>(dstrect->x * dbpp));
    while (pos >= 0x10000L) {
      srcp = src->pixels + (src_row * src->pitch) +
             (static_cast<ptrdiff_t>(srcrect->x * sbpp));
      ++src_row;
      pos -= 0x10000L;
    }
    if (sbpp == 1 && dbpp == 4) {
      copy_row_or1to4(srcp, srcrect->w, reinterpret_cast<uint32_t*>(dstp),
                      dstrect->w, src->palette);
    } else {
      switch (dbpp) {
        case 1:
          copy_row_or1(srcp, srcrect->w, dstp, dstrect->w);
          break;
        case 2:
          copy_row_or2(reinterpret_cast<uint16_t*>(srcp), srcrect->w,
                       reinterpret_cast<uint16_t*>(dstp), dstrect->w);
          break;
        case 3:
          copy_row3(srcp, srcrect->w, dstp, dstrect->w);
          break;
        case 4:
          copy_row_or4(reinterpret_cast<uint32_t*>(srcp), srcrect->w,
                       reinterpret_cast<uint32_t*>(dstp), dstrect->w);
          break;
        default:
          break;
      }
    }
    pos += inc;
  }

  return 0;
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

auto font_print(int x, int y, const char* text, VideoSurface_t* surface,
                double kx, double ky) -> void {
  int i = 0, c = 0;
  VideoRect_t s{}, d{};

  if (!font_sfc || !text || !surface) return;

  for (i = 0; text[i] != 0; i++) {
    int row = 0;
    c = static_cast<uint8_t>(text[i]);

    if (c > 127) {
      c = '?';
    }

    row = c / chars_in_row;

    s.x = (c - (row * chars_in_row)) * (font_size_x + 1) + 1;
    s.y = (row) * (font_size_y + 1) + 1;
    s.h = font_size_y;
    s.w = font_size_x;

    d.x = static_cast<int>(x + i * font_size_x * kx);
    d.y = y;
    d.w = static_cast<int>(s.w * kx);
    d.h = static_cast<int>(s.h * ky);

    if (d.x >= surface->w) break;
    video_soft_stretch_or(font_sfc, &s, surface, &d);
  }
}

auto font_print_right(int x, int y, const char* text, VideoSurface_t* surface,
                      double kx, double ky) -> void {
  int i = 0, c = 0;
  VideoRect_t s{}, d{};

  if (!font_sfc || !text || !surface) return;

  x -= static_cast<int>(strlen(text) * font_size_x * kx);

  for (i = 0; text[i] != 0; i++) {
    int row = 0;
    c = static_cast<uint8_t>(text[i]);
    if (c > 127) {
      c = '?';
    }

    row = c / chars_in_row;
    s.x = (c - (row * chars_in_row)) * (font_size_x + 1) + 1;
    s.y = (row) * (font_size_y + 1) + 1;
    s.h = font_size_y;
    s.w = font_size_x;

    d.x = static_cast<int>(x + i * font_size_x * kx);
    d.y = y;
    d.w = static_cast<int>(s.w * kx);
    d.h = static_cast<int>(s.h * ky);

    if (d.x >= surface->w) break;
    video_soft_stretch_or(font_sfc, &s, surface, &d);
  }
}

auto font_print_centered(int x, int y, const char* text,
                         VideoSurface_t* surface, double kx, double ky)
    -> void {
  int i = 0, c = 0;
  VideoRect_t s{}, d{};

  if (!font_sfc || !text || !surface) return;

  x -= static_cast<int>(strlen(text) * font_size_x * kx / 2);
  if (x < 0) {
    x = 0;
  }

  for (i = 0; text[i] != 0; i++) {
    int row = 0;
    c = static_cast<uint8_t>(text[i]);
    if (c > 127) {
      c = '?';
    }

    row = c / chars_in_row;
    s.x = (c - (row * chars_in_row)) * (font_size_x + 1) + 1;
    s.y = (row) * (font_size_y + 1) + 1;
    s.h = font_size_y;
    s.w = font_size_x;

    d.x = static_cast<int>(x + i * font_size_x * kx);
    d.y = y;
    d.w = static_cast<int>(s.w * kx);
    d.h = static_cast<int>(s.h * ky);

    if (d.x >= surface->w) break;
    video_soft_stretch_or(font_sfc, &s, surface, &d);
  }
}

auto surface_fader(VideoSurface_t* surface, float r_factor, float g_factor,
                   float b_factor, float a_factor, VideoRect_t* r) -> void {
  (void)a_factor;
  (void)r;
  int i = 0;
  VideoColor_t* colors = nullptr;

  if (!surface || surface->bpp != 1) {
    return;
  }

  colors = surface->palette;
  for (i = 0; i < 256; i++) {
    colors[i].r = static_cast<uint8_t>(colors[i].r * r_factor);
    colors[i].g = static_cast<uint8_t>(colors[i].g * g_factor);
    colors[i].b = static_cast<uint8_t>(colors[i].b * b_factor);
  }
}

auto putpixel(VideoSurface_t* surface, int x, int y, uint32_t pixel) -> void {
  if (!surface || !surface->pixels || x < 0 || x >= surface->w || y < 0 ||
      y >= surface->h) {
    return;
  }

  uint8_t* p = surface->pixels + y * surface->pitch +
               static_cast<ptrdiff_t>(x * surface->bpp);

  switch (surface->bpp) {
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

auto rectangle(VideoSurface_t* surface, int x, int y, int w, int h,
               uint32_t pixel) -> void {
  if (!surface) return;
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

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
