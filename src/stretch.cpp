#include "stdafx.h"
#include "Video.h"
#include "stretch.h"
#include <cstring>

template <typename T>
static void CopyRow(T *src, int src_w, T *dst, int dst_w)
{
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
static void CopyRowOr(T *src, int src_w, T *dst, int dst_w)
{
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

static void copy_row1(uint8_t *src, int src_w, uint8_t *dst, int dst_w) { CopyRow(src, src_w, dst, dst_w); }
static void copy_row2(uint16_t *src, int src_w, uint16_t *dst, int dst_w) { CopyRow(src, src_w, dst, dst_w); }
static void copy_row4(uint32_t *src, int src_w, uint32_t *dst, int dst_w) { CopyRow(src, src_w, dst, dst_w); }
static void copy_row_or1(uint8_t *src, int src_w, uint8_t *dst, int dst_w) { CopyRowOr(src, src_w, dst, dst_w); }
static void copy_row_or2(uint16_t *src, int src_w, uint16_t *dst, int dst_w) { CopyRowOr(src, src_w, dst, dst_w); }
static void copy_row_or4(uint32_t *src, int src_w, uint32_t *dst, int dst_w) { CopyRowOr(src, src_w, dst, dst_w); }

static uint32_t g_palette_lut[256];
static VideoColor* g_last_palette = NULL;

static void UpdatePaletteLUT(VideoColor* palette) {
    if (!palette) return;
    if (palette == g_last_palette) return;

    for (int i = 0; i < 256; ++i) {
        g_palette_lut[i] = (palette[i].r << 16) | (palette[i].g << 8) | palette[i].b;
    }
    g_last_palette = palette;
}

static void copy_row1to4(uint8_t *src, int src_w, uint32_t *dst, int dst_w, VideoColor *palette)
{
  UpdatePaletteLUT(palette);
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

static void copy_row_or1to4(uint8_t *src, int src_w, uint32_t *dst, int dst_w, VideoColor *palette)
{
  UpdatePaletteLUT(palette);
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

static void copy_row3(uint8_t *src, int src_w, uint8_t *dst, int dst_w) {
  int i;
  int pos, inc;
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

int VideoSoftStretch(VideoSurface *src, VideoRect *srcrect, VideoSurface *dst, VideoRect *dstrect) {
  int pos, inc;
  int dst_maxrow;
  int src_row, dst_row;
  uint8_t *srcp = NULL;
  uint8_t *dstp;
  VideoRect full_src;
  VideoRect full_dst;
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

  pos = 0x10000;
  inc = (srcrect->h << 16) / dstrect->h;
  src_row = srcrect->y;
  dst_row = dstrect->y;

  for (dst_maxrow = dst_row + dstrect->h; dst_row < dst_maxrow; ++dst_row) {
    dstp = (uint8_t *) dst->pixels + (dst_row * dst->pitch) + (dstrect->x * dbpp);
    while (pos >= 0x10000L) {
      srcp = (uint8_t *) src->pixels + (src_row * src->pitch) + (srcrect->x * sbpp);
      ++src_row;
      pos -= 0x10000L;
    }
    if (sbpp == 1 && dbpp == 4) {
        copy_row1to4(srcp, srcrect->w, (uint32_t *)dstp, dstrect->w, src->palette);
    } else {
        switch (dbpp) {
          case 1:
            copy_row1(srcp, srcrect->w, dstp, dstrect->w);
            break;
          case 2:
            copy_row2((uint16_t *) srcp, srcrect->w, (uint16_t *) dstp, dstrect->w);
            break;
          case 3:
            copy_row3(srcp, srcrect->w, dstp, dstrect->w);
            break;
          case 4:
            copy_row4((uint32_t *) srcp, srcrect->w, (uint32_t *) dstp, dstrect->w);
            break;
        }
    }
    pos += inc;
  }

  return (0);
}

static void copy8mono(uint8_t *src, int src_w, uint8_t *dst, int dst_w, uint8_t fgbrush, uint8_t bgbrush) {
  int i;
  int pos, inc;
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

static void copy8mono4(uint8_t *src, int src_w, uint32_t *dst, int dst_w, uint32_t fgbrush, uint32_t bgbrush) {
  int i;
  int pos, inc;
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

int VideoSoftStretchMono8(VideoSurface *src, VideoRect *srcrect, VideoSurface *dst, VideoRect *dstrect, uint32_t fgbrush, uint32_t bgbrush)
{
  int pos, inc;
  int dst_maxrow;
  int src_row, dst_row;
  uint8_t *srcp = NULL;
  uint8_t *dstp;
  VideoRect full_src;
  VideoRect full_dst;
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

  pos = 0x10000;
  inc = (srcrect->h << 16) / dstrect->h;
  src_row = srcrect->y;
  dst_row = dstrect->y;

  for (dst_maxrow = dst_row + dstrect->h; dst_row < dst_maxrow; ++dst_row) {
    dstp = (uint8_t *) dst->pixels + (dst_row * dst->pitch) + (dstrect->x * dbpp);
    while (pos >= 0x10000L) {
      srcp = (uint8_t *) src->pixels + (src_row * src->pitch) + (srcrect->x * sbpp);
      ++src_row;
      pos -= 0x10000L;
    }
    if (sbpp == 1 && dbpp == 4) {
        copy8mono4(srcp, srcrect->w, (uint32_t *)dstp, dstrect->w, fgbrush, bgbrush);
    } else {
        switch (dbpp) {
          case 1:
            copy8mono(srcp, srcrect->w, dstp, dstrect->w, (uint8_t)fgbrush, (uint8_t)bgbrush);
            break;
          default:
            break;
        }
    }
    pos += inc;
  }

  return (0);
}

int VideoSoftStretchOr(VideoSurface *src, VideoRect *srcrect, VideoSurface *dst, VideoRect *dstrect) {
  int pos, inc;
  int dst_maxrow;
  int src_row, dst_row;
  uint8_t *srcp = NULL;
  uint8_t *dstp;
  VideoRect full_src;
  VideoRect full_dst;
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

  pos = 0x10000;
  inc = (srcrect->h << 16) / dstrect->h;
  src_row = srcrect->y;
  dst_row = dstrect->y;

  for (dst_maxrow = dst_row + dstrect->h; dst_row < dst_maxrow; ++dst_row) {
    dstp = (uint8_t *) dst->pixels + (dst_row * dst->pitch) + (dstrect->x * dbpp);
    while (pos >= 0x10000L) {
      srcp = (uint8_t *) src->pixels + (src_row * src->pitch) + (srcrect->x * sbpp);
      ++src_row;
      pos -= 0x10000L;
    }
    if (sbpp == 1 && dbpp == 4) {
        copy_row_or1to4(srcp, srcrect->w, (uint32_t *)dstp, dstrect->w, src->palette);
    } else {
        switch (dbpp) {
          case 1:
            copy_row_or1(srcp, srcrect->w, dstp, dstrect->w);
            break;
          case 2:
            copy_row_or2((uint16_t *) srcp, srcrect->w, (uint16_t *) dstp, dstrect->w);
            break;
          case 3:
            copy_row3(srcp, srcrect->w, dstp, dstrect->w);
            break;
          case 4:
            copy_row_or4((uint32_t *) srcp, srcrect->w, (uint32_t *) dstp, dstrect->w);
            break;
        }
    }
    pos += inc;
  }

  return (0);
}

VideoSurface *font_sfc = NULL;

bool fonts_initialization(void) {
    // This will be handled by the frontend loading assets and passing them to core
    // Or core will load them using its own non-SDL loader.
    // For now, I'll let Video.cpp handle it.
    return true;
}

void fonts_termination(void) {
  if (font_sfc) {
    free(font_sfc->pixels);
    free(font_sfc);
    font_sfc = NULL;
  }
}

void font_print(int x, int y, const char *text, VideoSurface *surface, double kx, double ky)
{
  int i, c;
  VideoRect s, d;

  if (!font_sfc) return;

  for (i = 0; text[i] != 0 && x < surface->w; i++) {
    int row;
    c = (unsigned char)(text[i]);

    if (c > 127) {
      c = '?';
    }

    row = c / CHARS_IN_ROW;

    s.x = (c - (row * CHARS_IN_ROW)) * (FONT_SIZE_X + 1) + 1;
    s.y = (row) * (FONT_SIZE_Y + 1) + 1;
    s.h = FONT_SIZE_Y;
    s.w = FONT_SIZE_X;

    d.x = (int)(x + i * FONT_SIZE_X * kx);
    d.y = y;
    d.w = (int)(s.w * kx);
    d.h = (int)(s.h * ky);
    VideoSoftStretchOr(font_sfc, &s, surface, &d);
  }
}

void font_print_right(int x, int y, const char *text, VideoSurface *surface, double kx, double ky)
{
  int i, c;
  VideoRect s, d;

  if (!font_sfc) return;

  x -= (int)(strlen(text) * FONT_SIZE_X * kx);

  for (i = 0; text[i] != 0 && x < surface->w; i++) {
    int row;
    c = (unsigned char)(text[i]);
    if (c > 127) {
      c = '?';
    }

    row = c / CHARS_IN_ROW;
    s.x = (c - (row * CHARS_IN_ROW)) * (FONT_SIZE_X + 1) + 1;
    s.y = (row) * (FONT_SIZE_Y + 1) + 1;
    s.h = FONT_SIZE_Y;
    s.w = FONT_SIZE_X;

    d.x = (int)(x + i * FONT_SIZE_X * kx);
    d.y = y;
    d.w = (int)(s.w * kx);
    d.h = (int)(s.h * ky);
    VideoSoftStretchOr(font_sfc, &s, surface, &d);
  }
}

void font_print_centered(int x, int y, const char *text, VideoSurface *surface, double kx, double ky)
{
  int i, c;
  VideoRect s, d;

  if (!font_sfc) return;

  x -= (int)(strlen(text) * FONT_SIZE_X * kx / 2);
  if (x < 0) {
    x = 0;
  }

  for (i = 0; text[i] != 0 && ((x * kx) < surface->w); i++) {
    int row;
    c = (unsigned char)(text[i]);
    if (c > 127) {
      c = '?';
    }

    row = c / CHARS_IN_ROW;
    s.x = (c - (row * CHARS_IN_ROW)) * (FONT_SIZE_X + 1) + 1;
    s.y = (row) * (FONT_SIZE_Y + 1) + 1;
    s.h = FONT_SIZE_Y;
    s.w = FONT_SIZE_X;

    d.x = (int)(x + i * FONT_SIZE_X * kx);
    d.y = y;
    d.w = (int)(s.w * kx);
    d.h = (int)(s.h * ky);
    VideoSoftStretchOr(font_sfc, &s, surface, &d);
  }
}

void surface_fader(VideoSurface *surface, float r_factor, float g_factor, float b_factor, float a_factor, VideoRect *r) {
  (void)a_factor;
  (void)r;
  int i;
  VideoColor *colors;

  if (surface->bpp != 1) {
    return;
  }

  colors = surface->palette;
  for (i = 0; i < 256; i++) {
    colors[i].r = (uint8_t)(colors[i].r * r_factor);
    colors[i].g = (uint8_t)(colors[i].g * g_factor);
    colors[i].b = (uint8_t)(colors[i].b * b_factor);
  }
}

void putpixel(VideoSurface *surface, int x, int y, uint32_t pixel) {
  if (x < 0 || x >= surface->w || y < 0 || y >= surface->h) {
    return;
  }

  uint8_t *p = (uint8_t *) surface->pixels + y * surface->pitch + x * surface->bpp;

  switch (surface->bpp) {
    case 1:
      *p = (uint8_t)pixel;
      break;
    case 2:
      *(uint16_t *) p = (uint16_t)pixel;
      break;
    case 4:
      *(uint32_t *) p = pixel;
      break;
  }
}

void rectangle(VideoSurface *surface, int x, int y, int w, int h, uint32_t pixel) {
  int i;

  for (i = 0; i < w; i++) {
    putpixel(surface, x + i, y, pixel);
    putpixel(surface, x + i, y + h, pixel);
  }
  for (i = 0; i <= h; i++) {
    putpixel(surface, x, y + i, pixel);
    putpixel(surface, x + w, y + i, pixel);
  }
}
