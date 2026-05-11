/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski, Nick Westgate

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Emulation of video modes
 *
 * Author: Various
 */

#include "core/Common.h"
#include <cstddef>
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <cstdio>

#include "core/asset.h"
#include "apple2/Video.h"

static auto GetTickCount() -> uint32_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#include "apple2/Memory.h"
#include "apple2/CPU.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/Peripheral.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Log.h"
#include "core/Common_Globals.h"
#include "core/Util_Text.h"
#include "apple2/stretch.h"
#include "apple2/Structs.h"
#include "apple2/peripherals/harddisk/Harddisk.h"

#include "charset40.xpm"
#include "charset40_IIplus.xpm"
#include "charset40_british.xpm"
#include "charset40_french.xpm"
#include "charset40_german.xpm"

static uint32_t g_pVideoOutput[VIDEO_WIDTH * VIDEO_HEIGHT];

auto VideoGetOutputBuffer() -> uint32_t* {
  return g_pVideoOutput;
}

auto VideoCreateSurface(int w, int h, int bpp) -> VideoSurface* {
  auto* s = static_cast<VideoSurface*>(calloc(1, sizeof(VideoSurface)));
  s->w = w;
  s->h = h;
  s->bpp = bpp;
  s->pitch = w * bpp;
  s->pixels = static_cast<uint8_t*>(calloc(1, static_cast<size_t>(s->pitch * h)));
  return s;
}

void VideoDestroySurface(VideoSurface* s) {
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

auto VideoLoadXPM(const char * const *xpm) -> VideoSurface* {
  int w = 0, h = 0, colors = 0, cpp = 0;
  if (sscanf(xpm[0], "%d %d %d %d", &w, &h, &colors, &cpp) != 4) return nullptr;
  if (cpp != 1) return nullptr; // Only support 1 char per pixel for simplicity

  VideoSurface* s = VideoCreateSurface(w, h, 1);
  struct { char c; VideoColor color; } palette_map[256];
  for (int i = 0; i < colors; ++i) {
    char c = 0;
    char color_str[16];
    if (sscanf(xpm[i + 1], "%c c %s", &c, color_str) == 2) {
      palette_map[i].c = c;
      if (color_str[0] == '#') {
          palette_map[i].color.r = (hex_to_int(color_str[1]) << 4) | hex_to_int(color_str[2]);
          palette_map[i].color.g = (hex_to_int(color_str[3]) << 4) | hex_to_int(color_str[4]);
          palette_map[i].color.b = (hex_to_int(color_str[5]) << 4) | hex_to_int(color_str[6]);
          palette_map[i].color.a = 255;
      } else if (strcmp(color_str, "None") == 0) {
          palette_map[i].color = {0, 0, 0, 0};
      } else if (strcmp(color_str, "black") == 0) {
          palette_map[i].color = {0, 0, 0, 255};
      } else {
          palette_map[i].color = {255, 255, 255, 255}; // Default to white
      }
      s->palette[i] = palette_map[i].color;
    }
  }

  for (int y = 0; y < h; ++y) {
    const char* line = xpm[1 + colors + y];
    for (int x = 0; x < w; ++x) {
      char c = line[x];
      for (int i = 0; i < colors; ++i) {
        if (palette_map[i].c == c) {
          s->pixels[y * s->pitch + x] = i;
          break;
        }
      }
    }
  }
  return s;
}

#define GetRValue(rgb)      ((uint8_t)(rgb))
#define GetGValue(rgb)      ((uint8_t)(((uint16_t)(rgb)) >> 8))
#define GetBValue(rgb)      ((uint8_t)((rgb)>>16))
#define FLASH_80_COL 1
#define HALF_SHIFT_DITHER 0

const int SRCOFFS_40COL = 0;
const int SRCOFFS_80COL = (SRCOFFS_40COL + 256);
const int SRCOFFS_LORES = (SRCOFFS_80COL + 128);
const int SRCOFFS_HIRES = (SRCOFFS_LORES + 16);
const int SRCOFFS_DHIRES = (SRCOFFS_HIRES + 512);
const int SRCOFFS_TOTAL = (SRCOFFS_DHIRES + 2560);

#define  SW_80COL         (g_uVideoMode & VF_80COL)
#define  SW_DHIRES        (g_uVideoMode & VF_DHIRES)
#define  SW_HIRES         (g_uVideoMode & VF_HIRES)
#define  SW_MASK2         (g_uVideoMode & VF_MASK2)
#define  SW_MIXED         (g_uVideoMode & VF_MIXED)
#define  SW_PAGE2         (g_uVideoMode & VF_PAGE2)
#define  SW_TEXT          (g_uVideoMode & VF_TEXT)

#define  SWL_80COL         (vidmode_latched & VF_80COL)
#define  SWL_DHIRES        (vidmode_latched & VF_DHIRES)
#define  SWL_HIRES         (vidmode_latched & VF_HIRES)
#define  SWL_MASK2         (vidmode_latched & VF_MASK2)
#define  SWL_MIXED         (vidmode_latched & VF_MIXED)
#define  SWL_PAGE2         (vidmode_latched & VF_PAGE2)
#define  SWL_TEXT          (vidmode_latched & VF_TEXT)

#define  SOFTSTRECH(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, DST_W, DST_H) \
{ \
  VideoRect srcrect = {SRC_X, SRC_Y, SRC_W, SRC_H}; \
  VideoRect dstrect = {DST_X, DST_Y, DST_W, DST_H}; \
  VideoSoftStretch(SRC, &srcrect, DST, &dstrect);\
}

#define  SOFTSTRECH_MONO(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, DST_W, DST_H) \
{ \
  VideoRect srcrect = {SRC_X, SRC_Y, SRC_W, SRC_H}; \
  VideoRect dstrect = {DST_X, DST_Y, DST_W, DST_H}; \
  VideoSoftStretchMono8(SRC, &srcrect, DST, &dstrect, hBrush, 0);\
}

#define  SETSOURCEPIXEL(x, y, c)  g_aSourceStartofLine[(y)][(x)] = (c)
#define  SETFRAMECOLOR(i, r1, g1, b1)  framebufferinfo[i].r = r1; \
                                                           framebufferinfo[i].g = g1; \
framebufferinfo[i].b  = b1;

// video scanner constants
int const kHBurstClock = 53; // clock when Color Burst starts
int const kHBurstClocks = 4; // clocks per Color Burst duration
int const kHClock0State = 0x18; // H[543210] = 011000
int const kHClocks = 65; // clocks per horizontal scan (including HBL)
int const kHPEClock = 40; // clock when HPE (horizontal preset enable) goes low
int const kHPresetClock = 41; // clock when H state presets
int const kHSyncClock = 49; // clock when HSync starts
int const kHSyncClocks = 4; // clocks per HSync duration
int const kNTSCScanLines = 262; // total scan lines including VBL (NTSC)
int const kNTSCVSyncLine = 224; // line when VSync starts (NTSC)
int const kPALScanLines = 312; // total scan lines including VBL (PAL)
int const kPALVSyncLine = 264; // line when VSync starts (PAL)
int const kVLine0State = 0x100; // V[543210CBA] = 100000000
int const kVPresetLine = 256; // line when V state presets
int const kVSyncLines = 4; // lines per VSync duration

using UpdateFunc_t = bool (*)(int, int, int, int, int);

static uint8_t celldirty[TEXT_COLUMNS][DIRTY_CELL_ROWS];
static uint32_t customcolors[NUM_COLOR_PALETTE];  // MONOCHROME is last custom color

VideoSurface *g_hDeviceBitmap;
static uint8_t* framebufferbits;
VideoColor framebufferinfo[MAX_PALETTE_SIZE];

auto VideoGetOutputPalette() -> VideoColor* {
  return framebufferinfo;
}

const int MAX_FRAME_Y = VIDEO_HEIGHT;
static uint8_t* frameoffsettable[VIDEO_HEIGHT];
static uint8_t* g_pHiresBank1;
static uint8_t* g_pHiresBank0;

VideoSurface *g_hLogoBitmap = nullptr;
VideoSurface *charset40 = nullptr;
int multi_language_charset = false;

VideoSurface *g_hStatusSurface = nullptr;
int g_iStatusCycle = 0;

VideoSurface *g_origscreen = nullptr;
VideoSurface *g_hSourceBitmap = nullptr;

static uint8_t* g_pSourcePixels;
VideoColor g_pSourceHeader[MAX_PALETTE_SIZE];
const int MAX_SOURCE_Y = 512*2;
static uint8_t* g_aSourceStartofLine[MAX_SOURCE_Y];
static uint8_t* g_pTextBank1;
static uint8_t* g_pTextBank0;

static uint8_t hgrpixelmatrix[APPLE2_VISIBLE_WIDTH][APPLE2_VISIBLE_HEIGHT + 2 * HGR_MATRIX_YOFFSET];
static uint8_t colormixbuffer[6];
static uint16_t colormixmap[6][6][6];

static int g_nAltCharSetOffset = 0;
static bool displaypage2 = false;
static bool displaypage2_latched = false;
static uint8_t* framebufferaddr = (uint8_t*) nullptr;
static int framebufferpitch = 0;
bool graphicsmode = false;
static volatile bool hasrefreshed = false;
static uint32_t lastpageflip = 0;
uint32_t monochrome = RGB(DEFAULT_GRAY_COMPONENT, DEFAULT_GRAY_COMPONENT, DEFAULT_GRAY_COMPONENT);
static bool redrawfull = true;
static std::unique_ptr<uint8_t[], void(*)(void*)> vidlastmem(nullptr, free);
uint32_t g_uVideoMode = VF_TEXT;
uint32_t g_uDebugVideoMode = VF_TEXT;
static uint32_t vidmode_latched = VF_TEXT;
uint32_t g_videotype = VT_COLOR_STANDARD;
uint32_t g_singlethreaded = 1;
std::atomic<bool> g_bFrameReady(false);

static bool g_bTextFlashState = false;
static bool g_bTextFlashFlag = false;

bool g_ShowLeds = true;

const uint32_t nVBlStop_NTSC = 21;
const uint32_t nVBlStop_PAL = 29;

void DrawDHiResSource();
void DrawHiResSource();
void DrawHiResSourceHalfShiftFull();
void DrawHiResSourceHalfShiftDim();
void DrawLoResSource();
void DrawMonoDHiResSource();
void DrawMonoHiResSource();
void DrawMonoLoResSource();
void DrawMonoTextSource(VideoSurface *dc);
void DrawTextSource(VideoSurface *dc);

// Multithreaded

auto VideoInitWorker() -> bool;

std::thread video_worker_thread_;
static volatile bool video_worker_active_ = false;
static volatile bool video_worker_terminate_ = false;
static volatile bool video_worker_refresh_ = false;
std::recursive_mutex g_video_draw_mutex;
std::condition_variable video_cv;

static char display_pipeline_[0x2000*4 + 0x400*4];


void CopySource(int destx, int desty, int xsize, int ysize, int sourcex, int sourcey) {
  uint8_t* currdestptr = frameoffsettable[desty] + destx;
  uint8_t* currsourceptr = g_aSourceStartofLine[sourcey] + sourcex;
  while (ysize--) {
    if (ysize & 1 || VT_COLOR_TVEMU > g_videotype) {
      memcpy(currdestptr, currsourceptr, xsize);
    } else {
      memset(currdestptr, 0, xsize);
    }
    currdestptr += framebufferpitch;
    currsourceptr += SRCOFFS_TOTAL;
  }
}

void CreateFrameOffsetTable(uint8_t* addr, int pitch) {
  if (framebufferaddr == addr && framebufferpitch == pitch) {
    return;
  }
  framebufferaddr = addr;
  framebufferpitch = pitch;

  // CREATE THE OFFSET TABLE FOR EACH SCAN LINE IN THE FRAME BUFFER
  for (int loop = 0; loop < VIDEO_HEIGHT; loop++) {
    frameoffsettable[loop] = framebufferaddr + static_cast<ptrdiff_t>(framebufferpitch * loop);
  }
}

void CreateIdentityPalette() {
  memset(framebufferinfo, 0, MAX_PALETTE_SIZE * sizeof(VideoColor));
  // SET FRAME BUFFER TABLE ENTRIES TO CUSTOM COLORS
  SETFRAMECOLOR(DEEP_RED, 0xD0, 0x00, 0x30);
  SETFRAMECOLOR(LIGHT_BLUE, 0x60, 0xA0, 0xFF);
  SETFRAMECOLOR(BROWN, 0x80, 0x50, 0x00);
  SETFRAMECOLOR(ORANGE, 0xFF, 0x80, 0x00);
  SETFRAMECOLOR(PINK, 0xFF, 0x90, 0x80);
  SETFRAMECOLOR(AQUA, 0x40, 0xFF, 0x90);

  SETFRAMECOLOR(HGR_BLACK, 0x00, 0x00, 0x00);
  SETFRAMECOLOR(HGR_WHITE, 0xFF, 0xFF, 0xFE);
  SETFRAMECOLOR(HGR_BLUE, 0x00, 0x80, 0xFF);
  SETFRAMECOLOR(HGR_RED, 0xF0, 0x50, 0x00);
  SETFRAMECOLOR(HGR_GREEN, 0x20, 0xC0, 0x00);
  SETFRAMECOLOR(HGR_MAGENTA, 0xA0, 0x00, 0xFF);
  SETFRAMECOLOR(HGR_GREY1, 0x80, 0x80, 0x80);
  SETFRAMECOLOR(HGR_GREY2, 0x80, 0x80, 0x80);
  SETFRAMECOLOR(HGR_YELLOW, 0xD0, 0xB0, 0x10);
  SETFRAMECOLOR(HGR_AQUA, 0x20, 0xB0, 0xB0);
  SETFRAMECOLOR(HGR_PURPLE, 0x60, 0x50, 0xE0);
  SETFRAMECOLOR(HGR_PINK, 0xD0, 0x40, 0xA0);

  SETFRAMECOLOR(MONOCHROME_CUSTOM, GetRValue(monochrome), GetGValue(monochrome),
                GetBValue(monochrome));

  SETFRAMECOLOR(MONOCHROME_AMBER, 0xFF, 0x80, 0x00);
  SETFRAMECOLOR(MONOCHROME_GREEN, 0x00, 0xC0, 0x00);
  SETFRAMECOLOR(MONOCHROME_WHITE, 0xFF, 0xFF, 0xFF);
  SETFRAMECOLOR(BLACK, 0x00, 0x00, 0x00);
  SETFRAMECOLOR(DARK_RED, 0x80, 0x00, 0x00);
  SETFRAMECOLOR(DARK_GREEN, 0x00, 0x80, 0x00);
  SETFRAMECOLOR(DARK_YELLOW, 0x80, 0x80, 0x00);
  SETFRAMECOLOR(DARK_BLUE, 0x00, 0x00, 0x80);
  SETFRAMECOLOR(DARK_MAGENTA, 0x80, 0x00, 0x80);
  SETFRAMECOLOR(DARK_CYAN, 0x00, 0x80, 0x80);
  SETFRAMECOLOR(LIGHT_GRAY, 0xC0, 0xC0, 0xC0);
  SETFRAMECOLOR(MONEY_GREEN, 0xC0, 0xDC, 0xC0);
  SETFRAMECOLOR(SKY_BLUE, 0xA6, 0xCA, 0xF0);
  SETFRAMECOLOR(CREAM, 0xFF, 0xFB, 0xF0);
  SETFRAMECOLOR(MEDIUM_GRAY, 0xA0, 0xA0, 0xA4);
  SETFRAMECOLOR(DARK_GRAY, 0x80, 0x80, 0x80);
  SETFRAMECOLOR(RED, 0xFF, 0x00, 0x00);
  SETFRAMECOLOR(GREEN, 0x00, 0xFF, 0x00);
  SETFRAMECOLOR(YELLOW, 0xFF, 0xFF, 0x00);
  SETFRAMECOLOR(BLUE, 0x00, 0x00, 0xFF);
  SETFRAMECOLOR(MAGENTA, 0xFF, 0x00, 0xFF);
  SETFRAMECOLOR(CYAN, 0x00, 0xFF, 0xFF);
  SETFRAMECOLOR(WHITE, 0xFF, 0xFF, 0xFF);

  SETFRAMECOLOR(LIGHT_SKY_BLUE, 80, 192, 255);
  SETFRAMECOLOR(DARKER_SKY_BLUE, 0, 128, 192);
  SETFRAMECOLOR(DEEP_SKY_BLUE, 0,  64, 128 );
  SETFRAMECOLOR(DARKER_CYAN,   0, 63, 63 );
  SETFRAMECOLOR(DARKEST_CYAN,   0, 31, 31 );
  SETFRAMECOLOR(HALF_ORANGE, 128, 64,   0 );
  SETFRAMECOLOR(DARKER_BLUE, 0x00, 0x00, 63);
  SETFRAMECOLOR(DARKER_YELLOW, 0x00, 63, 63);
  SETFRAMECOLOR(DARKEST_YELLOW, 0x00, 31, 31);
  SETFRAMECOLOR(LIGHTEST_GRAY, 223, 223, 223);
  SETFRAMECOLOR(DARKER_GREEN, 0x00, 63, 0x00);
  SETFRAMECOLOR(DARKEST_GREEN, 0x00, 31, 0x00);
}

void CreateDIBSections() {
  g_video_draw_mutex.lock();

  memcpy(g_pSourceHeader,  framebufferinfo,  MAX_PALETTE_SIZE * sizeof(VideoColor));

  // CREATE THE FRAME BUFFER DIB SECTION
  if (g_hDeviceBitmap) {
    VideoDestroySurface(g_hDeviceBitmap);
  }
  g_hDeviceBitmap = VideoCreateSurface(VIDEO_WIDTH, VIDEO_HEIGHT, 1);

  if (g_origscreen) {
    VideoDestroySurface(g_origscreen);
  }
  g_origscreen = VideoCreateSurface(static_cast<int>(g_state.ScreenWidth), static_cast<int>(g_state.ScreenHeight), 1);

  if (g_hDeviceBitmap == nullptr || g_origscreen == nullptr) {
    fprintf(stderr, "g_hDeviceBitmap or g_origscreen was not created\n");
    g_video_draw_mutex.unlock();
    return;
  }

  framebufferbits = g_hDeviceBitmap->pixels;
  memcpy(g_hDeviceBitmap->palette, g_pSourceHeader, MAX_PALETTE_SIZE * sizeof(VideoColor));
  memcpy(g_origscreen->palette, g_pSourceHeader, MAX_PALETTE_SIZE * sizeof(VideoColor));

  if (g_hStatusSurface) {
    VideoDestroySurface(g_hStatusSurface);
  }
  g_hStatusSurface = VideoCreateSurface(STATUS_PANEL_W, STATUS_PANEL_H, 1);
  if (g_hStatusSurface == nullptr) {
    fprintf(stderr, "g_hStatusSurface was not created\n");
    g_video_draw_mutex.unlock();
    return;
  }
  memcpy(g_hStatusSurface->palette, g_pSourceHeader, MAX_PALETTE_SIZE * sizeof(VideoColor));

  /* Create status panel background */
  VideoRect srect{};
  uint8_t mybluez = DARK_BLUE;
  uint8_t myyell = YELLOW;

  srect.x = srect.y = 0;
  srect.w = STATUS_PANEL_W;
  srect.h = STATUS_PANEL_H;
  memset(g_hStatusSurface->pixels, mybluez, static_cast<size_t>(STATUS_PANEL_W * STATUS_PANEL_H));
  rectangle(g_hStatusSurface, 0, 0, STATUS_PANEL_W - 1, STATUS_PANEL_H - 1, myyell);
  rectangle(g_hStatusSurface, 2, 2, STATUS_PANEL_W - 5, STATUS_PANEL_H - 5, myyell);
  if (font_sfc == nullptr) {
    fonts_initialization();
}
  if (font_sfc != nullptr) {
    const float scale_x = 1.3f;
    const float scale_y = 1.5f;
    const int text_y = 6;
    font_print(7, text_y, "FDD1", g_hStatusSurface, scale_x, scale_y);
    font_print(40, text_y, "FDD2", g_hStatusSurface, scale_x, scale_y);
    font_print(74, text_y, "HDD", g_hStatusSurface, scale_x, scale_y);
  }
  // CREATE THE SOURCE IMAGE DIB SECTION
  if (g_hSourceBitmap) {
    VideoDestroySurface(g_hSourceBitmap);
  }
  g_hSourceBitmap = VideoCreateSurface(SRCOFFS_TOTAL, MAX_SOURCE_Y, 1);
  if (g_hSourceBitmap == nullptr) {
    fprintf(stderr, "g_hSourceBitmap was not created\n");
    g_video_draw_mutex.unlock();
    return;
  }

  g_pSourcePixels = g_hSourceBitmap->pixels;
  memcpy(g_hSourceBitmap->palette, framebufferinfo, 256 * sizeof(VideoColor));

  // CREATE THE OFFSET TABLE FOR EACH SCAN LINE IN THE SOURCE IMAGE
  for (int y = 0; y < MAX_SOURCE_Y; y++) {
    g_aSourceStartofLine[y] = g_pSourcePixels + static_cast<ptrdiff_t>(SRCOFFS_TOTAL * y);
  }

  // DRAW THE SOURCE IMAGE INTO THE SOURCE BIT BUFFER
  memset(g_pSourcePixels, 0, static_cast<size_t>(SRCOFFS_TOTAL * MAX_SOURCE_Y));

  if ((g_videotype != VT_MONO_CUSTOM) && (g_videotype != VT_MONO_AMBER) && (g_videotype != VT_MONO_GREEN) &&
      (g_videotype != VT_MONO_WHITE)) {
    DrawTextSource(g_hSourceBitmap);

    DrawLoResSource();
    if (g_videotype == VT_COLOR_HALF_SHIFT_DIM) {
      DrawHiResSourceHalfShiftDim();
    } else {
      DrawHiResSource();
    }
    DrawDHiResSource();
  } else {
    DrawMonoTextSource(g_hSourceBitmap);

    DrawMonoLoResSource();
    DrawMonoHiResSource();
    DrawMonoDHiResSource();
  }

  g_video_draw_mutex.unlock();
}

void DrawDHiResSource() {
  uint8_t colorval[16] = {BLACK, DARK_BLUE, DARK_GREEN, BLUE, BROWN, LIGHT_GRAY, GREEN, AQUA, DEEP_RED, MAGENTA, DARK_GRAY,
                       LIGHT_BLUE, ORANGE, PINK, YELLOW, WHITE};

  #define OFFSET  3
  #define SIZE    10
  for (int column = 0; column < 256; column++) {
    int coloffs = SIZE * column;
    for (unsigned byteval = 0; byteval < 256; byteval++) {
      int color[SIZE];
      memset(color, 0, sizeof(color));
      unsigned pattern = (static_cast<uint16_t>((static_cast<uint8_t>(byteval)) | (static_cast<uint16_t>(static_cast<uint8_t>(column))) << 8));
      int pixel = 0;
      for (pixel = 1; pixel < 15; pixel++) {
        if (pattern & (1 << pixel)) {
          int pixelcolor = 1 << ((pixel - OFFSET) & 3);
          if ((pixel >= OFFSET + 2) && (pixel < SIZE + OFFSET + 2) && (pattern & (0x7 << (pixel - 4)))) {
            color[pixel - (OFFSET + 2)] |= pixelcolor;
          }
          if ((pixel >= OFFSET + 1) && (pixel < SIZE + OFFSET + 1) && (pattern & (0xF << (pixel - 4)))) {
            color[pixel - (OFFSET + 1)] |= pixelcolor;
          }
          if ((pixel >= OFFSET + 0) && (pixel < SIZE + OFFSET + 0)) {
            color[pixel - (OFFSET + 0)] |= pixelcolor;
          }
          if ((pixel >= OFFSET - 1) && (pixel < SIZE + OFFSET - 1) && (pattern & (0xF << (pixel + 1)))) {
            color[pixel - (OFFSET - 1)] |= pixelcolor;
          }
          if ((pixel >= OFFSET - 2) && (pixel < SIZE + OFFSET - 2) && (pattern & (0x7 << (pixel + 2)))) {
            color[pixel - (OFFSET - 2)] |= pixelcolor;
          }
        }
      }

      if (g_videotype == VT_COLOR_TEXT_OPTIMIZED) {
        // Activate for fringe reduction on white hgr text
        // drawback: loss of color mix patterns in hgr mode.
        // select g_videotype by index

        for (pixel = 0; pixel < 13; pixel++) {
          if ((pattern & (0xF << pixel)) == static_cast<unsigned>(0xF << pixel)) {
            for (int pos = pixel; pos < pixel + 4; pos++) {
              if (pos >= OFFSET && pos < SIZE + OFFSET) {
                color[pos - OFFSET] = 15;
              }
            }
          }
        }
      }

      int y = byteval << 1;
      for (int x = 0; x < SIZE; x++) {
        SETSOURCEPIXEL(SRCOFFS_DHIRES + coloffs + x, y, colorval[color[x]]);
        SETSOURCEPIXEL(SRCOFFS_DHIRES + coloffs + x, y + 1, colorval[color[x]]);
      }
    }
  }
  #undef SIZE
  #undef OFFSET
}


enum ColorMapping {
  CM_Magenta, CM_Blue, CM_Green, CM_Orange, CM_Black, CM_White, NUM_COLOR_MAPPING
};

const uint8_t aColorIndex[NUM_COLOR_MAPPING] = {HGR_MAGENTA, HGR_BLUE, HGR_GREEN, HGR_RED, HGR_BLACK, HGR_WHITE};

const uint8_t aColorDimmedIndex[NUM_COLOR_MAPPING] = {DARK_MAGENTA, // <- HGR_MAGENTA
                                                   DARK_BLUE, // <- HGR_BLUE
                                                   DARK_GREEN, // <- HGR_GREEN
                                                   DEEP_RED, // <- HGR_RED
                                                   HGR_BLACK, // no change
                                                   LIGHT_GRAY    // HGR_WHITE
};

void DrawHiResSourceHalfShiftDim() {
  for (int iColumn = 0; iColumn < 16; iColumn++) {
    int coloffs = iColumn << 5;

    for (unsigned iByte = 0; iByte < 256; iByte++) {
      int aPixels[11];

      aPixels[0] = iColumn & 4;
      aPixels[1] = iColumn & 8;
      aPixels[9] = iColumn & 1;
      aPixels[10] = iColumn & 2;

      int nBitMask = 1;
      int iPixel = 0;
      for (iPixel = 2; iPixel < 9; iPixel++) {
        aPixels[iPixel] = ((iByte & nBitMask) != 0);
        nBitMask <<= 1;
      }

      int hibit = ((iByte & 0x80) != 0);
      int x = 0;
      int y = iByte << 1;

      while (x < 28) {
        int adj = (x >= 14) << 1;
        int odd = (x >= 14);

        for (iPixel = 2; iPixel < 9; iPixel++) {
          int color = CM_Black;
          if (aPixels[iPixel]) {
            if (aPixels[iPixel - 1] || aPixels[iPixel + 1]) {
              color = CM_White;
            } else {
              color = ((odd ^ (iPixel & 1)) << 1) | hibit;
}
          } else if (aPixels[iPixel - 1] && aPixels[iPixel + 1]) {
            // Activate for fringe reduction on white hgr text -
            // drawback: loss of color mix patterns in hgr mode.
            // select g_videotype by index exclusion
            if (!(aPixels[iPixel - 2] && aPixels[iPixel + 2])) {
              color = ((odd ^ !(iPixel & 1)) << 1) | hibit;
}
          }

          /*
             Address Binary   -> Displayed
2000:01 0---0001 -> 1 0 0 0  column 1
2400:81 1---0001 ->  1 0 0 0 half-pixel shift right
2800:02 1---0010 -> 0 1 0 0  column 2

2000:02 column 2
2400:82 half-pixel shift right
2800:04 column 3

2000:03 0---0011 -> 1 1 0 0  column 1 & 2
2400:83 1---0011 ->  1 1 0 0 half-pixel shift right
2800:06 1---0110 -> 0 1 1 0  column 2 & 3

@reference: see Beagle Bro's Disk: "Silicon Salid", File: DOUBLE HI-RES
Fortunately double-hires is supported via pixel doubling, so we can do half-pixel shifts ;-)
*/
          switch (color) {
            case CM_Magenta:
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, HGR_MAGENTA);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y, DARK_MAGENTA);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1, HGR_MAGENTA);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1, DARK_MAGENTA);
              break;
            case CM_Blue   :
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y, HGR_BLUE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 2, y, DARK_BLUE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1, HGR_BLUE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 2, y + 1, DARK_BLUE);
              if (hibit) {
                if (iPixel <= 2) {
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, DARK_BLUE);
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1, DARK_BLUE);
                }
              }
              break;
            case CM_Green :
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, HGR_GREEN);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y, DARK_GREEN);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1, HGR_GREEN);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1, DARK_GREEN);
              break;
            case CM_Orange:
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y, HGR_RED);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 2, y, BROWN);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1, HGR_RED);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 2, y + 1, BROWN);
              if (hibit) {
                if (iPixel <= 2) {
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, BROWN);
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1, BROWN);
                }
              }
              break;
            case CM_Black :
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, HGR_BLACK);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y, HGR_BLACK);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1, HGR_BLACK);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1, HGR_BLACK);
              break;
            case CM_White :
              // Don't dither / half-shift white, since DROL cutscene looks bad :(
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, HGR_WHITE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y, HGR_WHITE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1, HGR_WHITE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1, HGR_WHITE);
              if (hibit) {
                if (iPixel <= 2) {
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, HGR_WHITE);
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1, HGR_WHITE);
                }
              }
              break;
            default:
              break;
          }
          x += 2;
        }
      }
    }
  }
}


void DrawHiResSource() {
  for (int iColumn = 0; iColumn < 16; iColumn++) {
    int coloffs = iColumn << 5;

    for (unsigned iByte = 0; iByte < 256; iByte++) {
      int aPixels[11];

      aPixels[0] = iColumn & 4;
      aPixels[1] = iColumn & 8;
      aPixels[9] = iColumn & 1;
      aPixels[10] = iColumn & 2;

      int nBitMask = 1;
      int iPixel = 0;
      for (iPixel = 2; iPixel < 9; iPixel++) {
        aPixels[iPixel] = ((iByte & nBitMask) != 0);
        nBitMask <<= 1;
      }

      int hibit = ((iByte & 0x80) != 0);
      int x = 0;
      int y = iByte << 1;

      while (x < 28) {
        int adj = (x >= 14) << 1;
        int odd = (x >= 14);

        for (iPixel = 2; iPixel < 9; iPixel++) {
          int color = CM_Black;
          if (aPixels[iPixel]) {
            if (aPixels[iPixel - 1] || aPixels[iPixel + 1]) {
              color = CM_White;
            } else {
              color = ((odd ^ (iPixel & 1)) << 1) | hibit;
}
          } else if (aPixels[iPixel - 1] && aPixels[iPixel + 1]) {
            if ((g_videotype == VT_COLOR_STANDARD) || (g_videotype == VT_COLOR_TVEMU) ||
                !(aPixels[iPixel - 2] && aPixels[iPixel + 2])) {
              color = ((odd ^ !(iPixel & 1)) << 1) | hibit;
            }
          }

          SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, aColorIndex[color]);
          SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y, aColorIndex[color]);

          if (VT_COLOR_TVEMU > g_videotype) {
            SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1, aColorIndex[color]);
            SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1, aColorIndex[color]);
          } else {
            SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1, aColorIndex[0]);
            SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1, aColorIndex[0]);
          }

          x += 2;
        }
      }
    }
  }
}


void DrawLoResSource() {
  uint8_t colorval[16] = {BLACK, DEEP_RED, DARK_BLUE, MAGENTA, DARK_GREEN, DARK_GRAY, BLUE, LIGHT_BLUE, BROWN, ORANGE,
                       LIGHT_GRAY, PINK, GREEN, YELLOW, AQUA, WHITE};
  for (int color = 0; color < 16; color++) {
    for (int x = 0; x < 16; x++) {
      for (int y = 0; y < 16; y++) {
        SETSOURCEPIXEL(SRCOFFS_LORES + x, (color << 4) + y, colorval[color]);
      }
    }
  }
}

auto GetMonochromeIndex() -> int {
  int iMonochrome = 0;

  switch (g_videotype) {
    case VT_MONO_AMBER:
      iMonochrome = MONOCHROME_AMBER;
      break;
    case VT_MONO_GREEN:
      iMonochrome = MONOCHROME_GREEN;
      break;
    case VT_MONO_WHITE:
      iMonochrome = MONOCHROME_WHITE;
      break;
    default           :
      iMonochrome = MONOCHROME_CUSTOM;
      break;
  }

  return iMonochrome;
}

void DrawMonoDHiResSource() {
  int iMonochrome = GetMonochromeIndex();

  for (int column = 0; column < 256; column++) {
    int coloffs = 10 * column;
    for (unsigned byteval = 0; byteval < 256; byteval++) {
      unsigned pattern = (static_cast<uint16_t>((static_cast<uint8_t>(byteval)) | (static_cast<uint16_t>(static_cast<uint8_t>(column))) << 8));
      int y = byteval << 1;
      for (int x = 0; x < 10; x++) {
        uint8_t colorval = pattern & (1 << (x + 3)) ? iMonochrome : BLACK;

        SETSOURCEPIXEL(SRCOFFS_DHIRES + coloffs + x, y, colorval);
        SETSOURCEPIXEL(SRCOFFS_DHIRES + coloffs + x, y + 1, colorval);
      }
    }
  }
}

void DrawMonoHiResSource() {
  int iMonochrome = GetMonochromeIndex();

  for (int column = 0; column < 512; column += 16) {
    for (int y = 0; y < 512; y += 2) {
      unsigned val = (y >> 1);
      for (int x = 0; x < 16; x += 2) {
        uint8_t colorval = (val & 1) ? iMonochrome : BLACK;
        val >>= 1;
        SETSOURCEPIXEL(SRCOFFS_HIRES + column + x, y, colorval);
        SETSOURCEPIXEL(SRCOFFS_HIRES + column + x + 1, y, colorval);
        SETSOURCEPIXEL(SRCOFFS_HIRES + column + x, y + 1, colorval);
        SETSOURCEPIXEL(SRCOFFS_HIRES + column + x + 1, y + 1, colorval);
      }
    }
  }
}

void DrawMonoLoResSource() {
  int iMonochrome = GetMonochromeIndex();
  for (int color = 0; color < 16; color++) {
    for (int x = 0; x < 16; x++) {
      for (int y = 0; y < 16; y++) {
        uint8_t colorval = (color >> (x & 3) & 1) ? iMonochrome : BLACK;
        SETSOURCEPIXEL(SRCOFFS_LORES + x, (color << 4) + y, colorval);
      }
}
}
}

void DrawMonoTextSource(VideoSurface *hDstDC) {
  if (charset40 == nullptr) {
    return;
  }
  uint8_t hBrush = 0;
  switch (g_videotype) {
    case VT_MONO_AMBER:
      hBrush = MONOCHROME_AMBER;
      break;
    case VT_MONO_GREEN:
      hBrush = MONOCHROME_GREEN;
      break;
    case VT_MONO_WHITE:
      hBrush = MONOCHROME_WHITE;
      break;
    default           :
      hBrush = MONOCHROME_CUSTOM;
      break;
  }

  if ((g_Apple2Type == A2TYPE_APPLE2)||
      (g_Apple2Type == A2TYPE_APPLE2PLUS))
  {
    SOFTSTRECH_MONO(charset40, 0, 0, 128, 128, hDstDC, SRCOFFS_40COL, 0, 256, 256);
  }
  else
  {
    int MaxLanguage = (multi_language_charset) ? 2 : 1;
    for (int Language=0;Language<MaxLanguage;Language++)
    {
      /* When ROM contains two character sets: US/default set is the second (starting at offset 128),
       * while the local language set is always the first (offset 0). */
      int srcYofs = ((Language==0)&&(multi_language_charset)) ? 128:0;
      int dstYofs = Language*(MAX_SOURCE_Y/2);

      SOFTSTRECH_MONO(charset40, 0, srcYofs, 128, 128, hDstDC, SRCOFFS_40COL, dstYofs, 256, 256);
      SOFTSTRECH_MONO(hDstDC, 0, dstYofs, 256, 256, hDstDC, SRCOFFS_40COL, 256+dstYofs, 256, 256);
      SOFTSTRECH_MONO(hDstDC, 0, dstYofs, 256, 64, hDstDC, SRCOFFS_40COL, 64+dstYofs, 256, 64);

      if (g_Apple2Type == A2TYPE_APPLE2E)
      {
        SOFTSTRECH_MONO(hDstDC, 0, 256+dstYofs, 256, 32, hDstDC, SRCOFFS_40COL, 256+64+dstYofs, 256, 32);
      }
    }

    SOFTSTRECH_MONO(hDstDC, 0, 0, 256, MAX_SOURCE_Y, hDstDC, SRCOFFS_80COL, 0, 128, MAX_SOURCE_Y);
  }
}

void DrawTextSource(VideoSurface *dc) {
  if (charset40 == nullptr) {
    return;
  }
  uint8_t hBrush = GetMonochromeIndex();

  if ((g_Apple2Type == A2TYPE_APPLE2)||
        (g_Apple2Type == A2TYPE_APPLE2PLUS))
  {
    SOFTSTRECH_MONO(charset40, 0, 0, 128, 128, dc, SRCOFFS_40COL, 0, 256, 256);
  }
  else
  {
    int MaxLanguage = (multi_language_charset) ? 2 : 1;
    for (int Language=0;Language<MaxLanguage;Language++)
    {
      /* When ROM contains two character sets: US/default set is the second (starting at offset 128),
       * while the local language set is always the first (offset 0). */
      int srcYofs = ((Language==0)&&(multi_language_charset)) ? 128:0;
      int dstYofs = Language*(MAX_SOURCE_Y/2);

      SOFTSTRECH_MONO(charset40, 0, srcYofs, 128, 128, dc, SRCOFFS_40COL, dstYofs, 256, 256);
      SOFTSTRECH_MONO(dc, 0, dstYofs, 256, 256, dc, SRCOFFS_40COL, 256+dstYofs, 256, 256);
      SOFTSTRECH_MONO(dc, 0, dstYofs, 256, 64, dc, SRCOFFS_40COL, 64+dstYofs, 256, 64);

      if (g_Apple2Type == A2TYPE_APPLE2E)
      {
        SOFTSTRECH_MONO(dc, 0, 256+dstYofs, 256, 32, dc, SRCOFFS_40COL, 256+64+dstYofs, 256, 32);
      }
    }

    SOFTSTRECH_MONO(dc, 0, 0, 256, MAX_SOURCE_Y, dc, SRCOFFS_80COL, 0, 128, MAX_SOURCE_Y);
  }
}

void SetLastDrawnImage() {
  if (vidlastmem == nullptr) {
    return;
}
  memcpy(vidlastmem.get() + 0x400, g_pTextBank0, 0x400);
  if (SWL_HIRES) {
    memcpy(vidlastmem.get() + 0x2000, g_pHiresBank0, 0x2000);
  }
  if (SWL_DHIRES && SWL_HIRES) {
    memcpy(vidlastmem.get(), g_pHiresBank1, 0x2000);
  } else if (SWL_80COL) { // Don't test for !SWL_HIRES, as some 80-col text routines have SWL_HIRES set (Bug #8300)
    memcpy(vidlastmem.get(), g_pTextBank1, 0x400);
  }
  int loop = 0;
  for (loop = 0; loop < 256; loop++) {
    *(memdirty + loop) &= ~2;
  }
}

// GPH: These "Update" functions update the SDL graphics buffer to be
// displayed on the host with what the "Draw" functions have
// drawn into the guest Apple graphics buffers.

// Update40Col
// This copies the literal Apple ROM font pixels
// to the graphical display buffer.
auto Update40ColCell(int x, int y, int xpixel, int ypixel, int offset) -> bool {
  (void)x;
  (void)y;
  uint8_t ch = *(g_pTextBank0 + offset);
  bool bCharChanged = (ch != *(vidlastmem.get() + offset + 0x400) || redrawfull || video_worker_active_);

  // FLASHing chars:
  // - FLASHing if:Alt Char Set is OFF && 0x40<=char<=0x7F
  // - The inverse of this char is located at: char+0x40
  bool bCharFlashing = (g_nAltCharSetOffset == 0) && (ch >= 0x40) && (ch <= 0x7F);

  if (bCharChanged || (bCharFlashing && g_bTextFlashFlag) ) {
    bool bInvert = bCharFlashing ? g_bTextFlashState : false;

    CopySource(xpixel, ypixel, APPLE_FONT_WIDTH, APPLE_FONT_HEIGHT,
               SRCOFFS_40COL + ((ch & 0x0F) << 4),
               (ch & 0xF0) + g_nAltCharSetOffset + (bInvert ? 0x40 : 0x00) +
               ((language_rocker_switch && multi_language_charset) ? 512:0));
    return true;
  }
  return false;
}

inline auto Update80ColumnCell(uint8_t c, const int xPixel, const int yPixel, bool bCharFlashing) -> bool {
  bool bInvert = bCharFlashing ? g_bTextFlashState : false;
  CopySource(xPixel, yPixel, (APPLE_FONT_WIDTH / 2), APPLE_FONT_HEIGHT, SRCOFFS_80COL + ((c & 15) << 3),
             ((c >> 4) << 4) + g_nAltCharSetOffset + (bInvert ? 0x40 : 0x00) +
             ((language_rocker_switch && multi_language_charset) ? 512:0));
  return true;
}

auto Update80ColCell(int x, int y, int xpixel, int ypixel, int offset) -> bool {
  (void)x;
  (void)y;
  bool bDirty = false;

  #if FLASH_80_COL
  uint8_t c1 = *(g_pTextBank1 + offset);
  uint8_t c0 = *(g_pTextBank0 + offset);

  bool bC1Changed = (c1 != *(vidlastmem.get() + offset + 0) || redrawfull || video_worker_active_);
  bool bC0Changed = (c0 != *(vidlastmem.get() + offset + 0x400) || redrawfull || video_worker_active_);

  bool bC1Flashing = (g_nAltCharSetOffset == 0) && (c1 >= 0x40) && (c1 <= 0x7F);
  bool bC0Flashing = (g_nAltCharSetOffset == 0) && (c0 >= 0x40) && (c0 <= 0x7F);

  if (bC1Changed || (bC1Flashing && g_bTextFlashFlag) ) {
    bDirty = Update80ColumnCell(c1, xpixel, ypixel, bC1Flashing);
  }

  if (bC0Changed || (bC0Flashing && g_bTextFlashFlag) ) {
    bDirty |= Update80ColumnCell(c0, xpixel + 7, ypixel, bC0Flashing);
  }
  #endif

  return bDirty;
}

auto UpdateDHiResCell(int x, int y, int xpixel, int ypixel, int offset) -> bool {
  (void)y;
  bool bDirty = false;
  int yoffset = 0;
  while (yoffset < 0x2000) {
    uint8_t byteval1 = (x > 0) ? *(g_pHiresBank0 + offset + yoffset - 1) : 0;
    uint8_t byteval2 = *(g_pHiresBank1 + offset + yoffset);
    uint8_t byteval3 = *(g_pHiresBank0 + offset + yoffset);
    uint8_t byteval4 = (x < 39) ? *(g_pHiresBank1 + offset + yoffset + 1) : 0;
    if ((byteval2 != *(vidlastmem.get() + offset + yoffset)) || (byteval3 != *(vidlastmem.get() + offset + yoffset + 0x2000)) ||
        ((x > 0) && ((byteval1 & 0x70) != (*(vidlastmem.get() + offset + yoffset + 0x1FFF) & 0x70))) ||
        ((x < 39) && ((byteval4 & 0x07) != (*(vidlastmem.get() + offset + yoffset + 1) & 0x07))) || redrawfull || video_worker_active_) {
      uint32_t dwordval =
        (byteval1 & 0x70) | ((byteval2 & 0x7F) << 7) | ((byteval3 & 0x7F) << 14) | ((byteval4 & 0x07) << 21);
      #define PIXEL  0
      #define COLOR  ((xpixel + PIXEL) & 3)
      #define VALUE  (dwordval >> (4 + PIXEL - COLOR))
      CopySource(xpixel + PIXEL, ypixel + (yoffset >> 9), 7, 2, SRCOFFS_DHIRES + 10 * (static_cast<uint8_t>((static_cast<uint16_t>(VALUE) >> 8) & 0xFF)) + COLOR,
                 (static_cast<uint8_t>(VALUE)) << 1);
      #undef PIXEL
      #define PIXEL  7
      CopySource(xpixel + PIXEL, ypixel + (yoffset >> 9), 7, 2, SRCOFFS_DHIRES + 10 * (static_cast<uint8_t>((static_cast<uint16_t>(VALUE) >> 8) & 0xFF)) + COLOR,
                 (static_cast<uint8_t>(VALUE)) << 1);
      #undef PIXEL
      #undef COLOR
      #undef VALUE
      bDirty = true;
    }
    yoffset += 0x400;
  }

  return bDirty;
}

auto MixColors(uint8_t c1, uint8_t c2) -> uint8_t
{
  #define COMBINATION(c1, c2, ref1, ref2) (((c1)==(ref1)&&(c2)==(ref2)) || ((c1)==(ref2)&&(c2)==(ref1)))

  if (c1 == c2) {
    return c1;
  }
  if (COMBINATION(c1, c2, HGR_BLUE, HGR_RED)) {
    return HGR_GREY1;
  } else if (COMBINATION(c1, c2, HGR_GREEN, HGR_MAGENTA)) {
    return HGR_GREY2;
  } else if (COMBINATION(c1, c2, HGR_RED, HGR_GREEN)) {
    return HGR_YELLOW;
  } else if (COMBINATION(c1, c2, HGR_BLUE, HGR_GREEN)) {
    return HGR_AQUA;
  } else if (COMBINATION(c1, c2, HGR_BLUE, HGR_MAGENTA)) {
    return HGR_PURPLE;
  } else if (COMBINATION(c1, c2, HGR_RED, HGR_MAGENTA)) {
    return HGR_PINK;
  } else {
    return MONOCHROME_CUSTOM; // visible failure indicator
  }

  #undef COMBINATION
}

void CreateColorMixMap() {
  int t = 0, m = 0, b = 0;
  uint8_t cTop = 0, cMid = 0, cBot = 0;
  uint16_t mixTop = 0, mixBot = 0;

  for (t = 0; t < 6; t++) {
    for (m = 0; m < 6; m++) {
      for (b = 0; b < 6; b++) {
        cTop = t | 0x10;
        cMid = m | 0x10;
        cBot = b | 0x10;
        if (cMid < HGR_BLUE) {
          mixTop = mixBot = cMid;
        } else {
          if (cTop < HGR_BLUE) {
            mixTop = 0x00;
          } else {
            mixTop = MixColors(cMid, cTop);
          }
          if (cBot < HGR_BLUE) {
            mixBot = 0x00;
          } else {
            mixBot = MixColors(cMid, cBot);
          }
          if (mixTop == 0x00 && mixBot != 0x00) {
            mixTop = mixBot;
          } else if (mixBot == 0x00 && mixTop != 0x00) {
            mixBot = mixTop;
          } else if (mixBot == 0x00 && mixTop == 0x00) {
            mixBot = mixTop = cMid;
          }
        }
        colormixmap[t][m][b] = (mixTop << 8) | mixBot;
      }
}
}
}

void MixColorsVertical(int matx, int maty)
{
  uint16_t twoHalfPixel = 0;
  int bot1idx = 0, bot2idx = 0;

  if (SW_MIXED && maty > 159) {
    if (maty < 161) {
      bot1idx = hgrpixelmatrix[matx][maty + 1] & 0x0F;
      bot2idx = 0;
    } else {
      bot1idx = bot2idx = 0;
    }
  } else {
    bot1idx = hgrpixelmatrix[matx][maty + 1] & 0x0F;
    bot2idx = hgrpixelmatrix[matx][maty + 2] & 0x0F;
  }

  twoHalfPixel = colormixmap[hgrpixelmatrix[matx][maty - 2] & 0x0F][hgrpixelmatrix[matx][maty - 1] & 0x0F][
    hgrpixelmatrix[matx][maty] & 0x0F];
  colormixbuffer[0] = (twoHalfPixel & 0xFF00) >> 8;
  colormixbuffer[1] = twoHalfPixel & 0x00FF;

  twoHalfPixel = colormixmap[hgrpixelmatrix[matx][maty - 1] & 0x0F][hgrpixelmatrix[matx][maty] & 0x0F][bot1idx];
  colormixbuffer[2] = (twoHalfPixel & 0xFF00) >> 8;
  colormixbuffer[3] = twoHalfPixel & 0x00FF;

  twoHalfPixel = colormixmap[hgrpixelmatrix[matx][maty] & 0x0F][bot1idx][bot2idx];
  colormixbuffer[4] = (twoHalfPixel & 0xFF00) >> 8;
  colormixbuffer[5] = twoHalfPixel & 0x00FF;
}

void CopyMixedSource(int x, int y, int sourcex, int sourcey) {
  uint8_t* currsourceptr = g_aSourceStartofLine[sourcey] + sourcex;
  uint8_t* currdestptr = frameoffsettable[y << 1] + (x << 1);
  uint8_t* currptr = nullptr;

  int matx = x;
  int maty = HGR_MATRIX_YOFFSET + y;
  int count = 0;
  int bufxoffset = 0;
  int hgrlinesabove = (y > 0) ? 1 : 0;
  int hgrlinesbelow = SW_MIXED ? ((y < 159) ? 1 : 0) : ((y < 191) ? 1 : 0);
  int i = 0;
  int istart = 2 - (hgrlinesabove << 1);
  int iend = 3 + (hgrlinesbelow << 1);

  for (count = 0, bufxoffset = 0; count < 7; count++, bufxoffset += 2) {
    hgrpixelmatrix[matx + count][maty] = *(currsourceptr + bufxoffset);
    MixColorsVertical(matx + count, maty);
    currptr = currdestptr + bufxoffset;
    if (hgrlinesabove) {
      currptr -= framebufferpitch << 1;
    }
    for (i = istart; i <= iend; currptr += framebufferpitch, i++) {
      if (~i & 1) {
        *currptr = *(currptr + 1) = colormixbuffer[i];
      } else {
        *currptr = 0;
      }
    }
  }
}

auto UpdateHiResCell(int x, int y, int xpixel, int ypixel, int offset) -> bool {
  (void)y;
  bool bDirty = false;
  int yoffset = 0;
  while (yoffset < 0x2000) {
    uint8_t byteval1 = (x > 0) ? *(g_pHiresBank0 + offset + yoffset - 1) : 0;
    uint8_t byteval2 = *(g_pHiresBank0 + offset + yoffset);
    uint8_t byteval3 = (x < 39) ? *(g_pHiresBank0 + offset + yoffset + 1) : 0;
    if ((byteval2 != *(vidlastmem.get() + offset + yoffset + 0x2000)) ||
        ((x > 0) && ((byteval1 & 0x60) != (*(vidlastmem.get() + offset + yoffset + 0x1FFF) & 0x60))) ||
        ((x < 39) && ((byteval3 & 0x03) != (*(vidlastmem.get() + offset + yoffset + 0x2001) & 0x03))) || redrawfull || video_worker_active_) {
      #define COLOFFS  (((byteval1 & 0x60) << 2) | \
    ((byteval3 & 0x03) << 5))
      if (g_videotype == VT_COLOR_TVEMU) {
        CopyMixedSource(xpixel >> 1, (ypixel + (yoffset >> 9)) >> 1, SRCOFFS_HIRES + COLOFFS + ((x & 1) << 4),
                        ((static_cast<int>(byteval2)) << 1));
      } else {
        CopySource(xpixel, ypixel + (yoffset >> 9), 14, 2, SRCOFFS_HIRES + COLOFFS + ((x & 1) << 4),
                   ((static_cast<int>(byteval2)) << 1));
      }
      #undef COLOFFS
      bDirty = true;
    }
    yoffset += 0x400;
  }

  return bDirty;
}

auto UpdateLoResCell(int x, int y, int xpixel, int ypixel, int offset) -> bool {
  (void)y;
  uint8_t val = *(g_pTextBank0 + offset);
  if ((val != *(vidlastmem.get() + offset + 0x400)) || redrawfull || video_worker_active_) {
    CopySource(xpixel, ypixel, 14, 8, SRCOFFS_LORES + ((x & 1) << 1), ((val & 0xF) << 4));
    CopySource(xpixel, ypixel + 8, 14, 8, SRCOFFS_LORES + ((x & 1) << 1), (val & 0xF0));
    return true;
  }
  return false;
}

auto UpdateDLoResCell(int x, int y, int xpixel, int ypixel, int offset) -> bool {
  (void)y;
  uint8_t auxval = *(g_pTextBank1 + offset);
  uint8_t mainval = *(g_pTextBank0 + offset);

  if ((auxval != *(vidlastmem.get() + offset)) || (mainval != *(vidlastmem.get() + offset + 0x400)) || redrawfull || video_worker_active_) {
    CopySource(xpixel, ypixel, 7, 8, SRCOFFS_LORES + ((x & 1) << 1), ((auxval & 0xF) << 4));
    CopySource(xpixel, ypixel + 8, 7, 8, SRCOFFS_LORES + ((x & 1) << 1), (auxval & 0xF0));
    CopySource(xpixel + 7, ypixel, 7, 8, SRCOFFS_LORES + ((x & 1) << 1), ((mainval & 0xF) << 4));
    CopySource(xpixel + 7, ypixel + 8, 7, 8, SRCOFFS_LORES + ((x & 1) << 1), (mainval & 0xF0));
    return true;
  }
  return false;
}

auto LoadCharset() -> VideoSurface* {
  VideoSurface *result = nullptr;

  if ((g_Apple2Type == A2TYPE_APPLE2)||
      (g_Apple2Type == A2TYPE_APPLE2PLUS))
  {
    // character bitmap for II and IIplus
    result = VideoLoadXPM(charset40_IIplus_xpm);
  }
  else
  {
    switch(g_Language)
    {
    case A2LANG_UK:
      result = VideoLoadXPM(charset40_british_xpm);
      break;
    case A2LANG_FR:
      result = VideoLoadXPM(charset40_french_xpm);
      break;
    case A2LANG_DE:
      result = VideoLoadXPM(charset40_german_xpm);
      break;
    case A2LANG_US: // fall-through
    default:
      // character bitmap for IIe and enhanced
      result = VideoLoadXPM(charset40_xpm);
    }
  }

  if (result)
  {
    /* correct character set bitmaps should be 128x128 (single language) or
     * 256x128 for the Euro-ROMs with alternative language */
    if (((result->h != 128)&&(result->h != 256))||
        (result->w != 128))
    {
      printf("ERROR: loaded character set has an unexpected size: %ix%i\n", result->w, result->h);
    }

    // enable second language support when charset has the double height (256 instead of 128 pixels)
    multi_language_charset = (result->h == 256);
    printf("Charset supports a second language: %s\n", (multi_language_charset)?"YES":"NO");
  }
  return result;
}

// All globally accessible functions are below this line

auto VideoApparentlyDirty() -> bool {
  if (SW_MIXED || redrawfull || video_worker_active_) {
    return true;
  }
  uint32_t address = (SW_HIRES && !SW_TEXT) ? (0x20 << displaypage2) : (0x4 << displaypage2);
  uint32_t length = (SW_HIRES && !SW_TEXT) ? 0x20 : 0x4;
  while (length--) {
    if (*(memdirty + (address++)) & 2) {
      return true;
    }
  }

  bool bCharFlashing = false;

  // Scan visible text page for any flashing chars
  if ((SW_TEXT || SW_MIXED) && (g_nAltCharSetOffset == 0)) {
    uint8_t *pnMemText = MemGetMainPtr(0x400 << displaypage2);

    // Scan 8 long-lines of 120 chars (at 128 char offsets):
    // . Skip 8-char holes in TEXT
    for (uint32_t y = 0; y < 8; y++) {
      for (uint32_t x = 0; x < 40 * 3; x++) {
        uint8_t ch = pnMemText[y * 128 + x];
        if ((ch >= 0x40) && (ch <= 0x7F)) {
          bCharFlashing = true;
          break;
        }
      }
    }
  }

  if (bCharFlashing) {
    return true;
  }
  return false;
}

void VideoBenchmark() {
  // Benchmark needs SDL_GetTicks and SDL_Delay, so we keep those.
  // But we replace any VideoSurface related calls.
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  int loop = 0;
  auto* mem32 = reinterpret_cast<uint32_t*>(mem);
  for (loop = 4096; loop < 6144; loop++) {
    *(mem32 + loop) = ((loop & 1) ^ ((loop & 0x40) >> 6)) ? 0x14141414 : 0xAAAAAAAA;
  }
  for (loop = 6144; loop < 8192; loop++) {
    *(mem32 + loop) = ((loop & 1) ^ ((loop & 0x40) >> 6)) ? 0xAAAAAAAA : 0x14141414;
  }

  uint32_t totaltextfps = 0;
  g_uVideoMode = VF_TEXT;
  memset(mem + 0x400,  0x14,  0x400);
  VideoRedrawScreen();
  auto milliseconds = static_cast<uint32_t>(GetTickCount());
  while (GetTickCount() == milliseconds);
  milliseconds = static_cast<uint32_t>(GetTickCount());
  uint32_t cycle = 0;
  do {
    if (cycle & 1) {
      memset(mem + 0x400,  0x14,  0x400);
    } else {
      memcpy(mem + 0x400,  mem + ((cycle & 2) ? 0x4000 : 0x6000),  0x400);
    }
    VideoRefreshScreen();
    if (cycle++ >= 3) {
      cycle = 0;
    }
    totaltextfps++;
  } while (GetTickCount() - milliseconds < 1000);

  uint32_t totalhiresfps = 0;
  g_uVideoMode = VF_HIRES;
  memset(mem + 0x2000,  0x14,  0x2000);
  VideoRedrawScreen();
  milliseconds = static_cast<uint32_t>(GetTickCount());
  while (GetTickCount() == milliseconds);
  milliseconds = static_cast<uint32_t>(GetTickCount());
  cycle = 0;
  do {
    if (cycle & 1) {
      memset(mem + 0x2000,  0x14,  0x2000);
    } else {
      memcpy(mem + 0x2000,  mem + ((cycle & 2) ? 0x4000 : 0x6000),  0x2000);
    }
    VideoRefreshScreen();
    if (cycle++ >= 3) {
      cycle = 0;
    }
    totalhiresfps++;
  } while (GetTickCount() - milliseconds < 1000);

  CpuSetupBenchmark();
  uint32_t totalmhz10 = 0;
  milliseconds = static_cast<uint32_t>(GetTickCount());
  while (GetTickCount() == milliseconds);
  milliseconds = static_cast<uint32_t>(GetTickCount());
  cycle = 0;
  do {
    CpuExecute(100000);
    totalmhz10++;
  } while (GetTickCount() - milliseconds < 1000);

  if ((regs.pc < 0x300) || (regs.pc > 0x400)) {
    printf("The emulator has detected a problem while running the CPU benchmark.\n");

    bool error = false;
    uint16_t lastpc = 0x300;
    int loop = 0;
    while ((loop < 10000) && !error) {
      CpuSetupBenchmark();
      CpuExecute(loop);
      if ((regs.pc < 0x300) || (regs.pc > 0x400)) {
        error = true;
      } else {
        lastpc = regs.pc;
        ++loop;
      }
    }
    if (error) {
      printf("The emulator experienced an error %u clock cycles into the CPU benchmark.\n", static_cast<unsigned>(loop));
      printf("Prior to the error, the program counter was at $%04X.\n", static_cast<unsigned>(lastpc));
      printf(" After the error, it had jumped to $%04X.\n", static_cast<unsigned>(regs.pc));
    } else {
      printf("The emulator was unable to locate the exact point of the error.\n");
    }
  }

  uint32_t realisticfps = 0;
  memset(mem + 0x2000,  0xAA,  0x2000);
  VideoRedrawScreen();
  milliseconds = static_cast<uint32_t>(GetTickCount());
  while (GetTickCount() == milliseconds);
  milliseconds = static_cast<uint32_t>(GetTickCount());
  cycle = 0;
  do {
    if (realisticfps < 10) {
      int cycles = 100000;
      while (cycles > 0) {
        uint32_t executedcycles = CpuExecute(static_cast<uint32_t>(103));
        cycles -= executedcycles;
        VideoUpdateVbl(0);
      }
    }
    if (cycle & 1) {
      memset(mem + 0x2000,  0xAA,  0x2000);
    } else {
      memcpy(mem + 0x2000,  mem + ((cycle & 2) ? 0x4000 : 0x6000),  0x2000);
    }
    VideoRefreshScreen();
    if (cycle++ >= 3) {
      cycle = 0;
    }
    realisticfps++;
  } while (GetTickCount() - milliseconds < 1000);
  printf("Pure Video FPS:\t%u hires, %u text\n", totalhiresfps, totaltextfps);
  printf("Pure CPU MHz:\t%u.%u%s\n\n", (totalmhz10 / 10), (totalmhz10 % 10),
         (IS_APPLE2() ? " (6502" : ""));
  printf("EXPECTED AVERAGE VIDEO GAME PERFORMANCE:\t%u FPS\n\n", realisticfps);
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
}

auto VideoCheckMode(uint16_t, uint16_t address, uint8_t, uint8_t, uint32_t nCyclesLeft) -> uint8_t {
  address &= 0xFF;
  if (address == 0x7F) {
    return MemReadFloatingBus(SW_DHIRES != 0, nCyclesLeft);
  } else {
    bool result = false;
    switch (address) {
      case 0x1A:
        result = SW_TEXT;
        break;
      case 0x1B:
        result = SW_MIXED;
        break;
      case 0x1D:
        result = SW_HIRES;
        break;
      case 0x1E:
        result = g_nAltCharSetOffset != 0;
        break;
      case 0x1F:
        result = SW_80COL;
        break;
      case 0x7F:
        result = SW_DHIRES;
        break;
    }
    return (MemReadFloatingBus(nCyclesLeft) & 0x7F) | (result ? 0x80 : 0);
  }
}

void VideoCheckPage(bool force) {
  if ((displaypage2 != (SW_PAGE2 != 0)) && (force || (emulmsec - lastpageflip > 500))) {
    displaypage2 = (SW_PAGE2 != 0);
    VideoRefreshScreen();
    lastpageflip = emulmsec;
  }
}

auto VideoCheckVbl(uint16_t, uint16_t, uint8_t, uint8_t, uint32_t nCyclesLeft) -> uint8_t {
  bool bVblBar = false;
  VideoGetScannerAddress(&bVblBar, nCyclesLeft);
  uint8_t r = MemReadFloatingBus(nCyclesLeft);
  return static_cast<uint8_t>((r & ~0x80) | ((bVblBar) ? 0x80 : 0));
}

void VideoChooseColor() {
}


void VideoDestroy() {
  // GPH Multithreaded
  {

    video_worker_terminate_ = true;
    if (video_worker_active_) {
      if (video_worker_thread_.joinable()) video_worker_thread_.join();
}
    video_worker_active_ = false;
  }
  // END GPH

  // Just free our surfaces and free vidlastmem
  // DESTROY BUFFERS
  vidlastmem.reset();
  // DESTROY FRAME BUFFER
  if (g_hDeviceBitmap) {
    VideoDestroySurface(g_hDeviceBitmap);
  }
  g_hDeviceBitmap = nullptr;

  if (g_origscreen) {
    VideoDestroySurface(g_origscreen);
  }
  g_origscreen = nullptr;

  if (g_hStatusSurface) {
    VideoDestroySurface(g_hStatusSurface);
  }
  g_hStatusSurface = nullptr;

  // DESTROY SOURCE IMAGE
  if (g_hSourceBitmap) {
    VideoDestroySurface(g_hSourceBitmap);
  }
  g_hSourceBitmap = nullptr;

  // DESTROY LOGO - ONLY IF IT WASN'T FROM ASSETS
  if (g_hLogoBitmap && (g_hLogoBitmap != assets->splash)) {
    VideoDestroySurface(g_hLogoBitmap);
  }
  g_hLogoBitmap = nullptr;

  if (charset40) {
    VideoDestroySurface(charset40);
  }
  charset40 = nullptr;

  if (font_sfc && (font_sfc != assets->font)) {
    VideoDestroySurface(font_sfc);
  }
  font_sfc = nullptr;
}

void VideoDisplayLogo() {
  VideoRect drect{}, srect{};

  if (!g_hLogoBitmap) {
    return; // nothing to display?
  }

  // Clear logo destination if needed, but normally we just stretch to it
  srect.x = srect.y = 0;
  srect.w = g_hLogoBitmap->w;
  srect.h = g_hLogoBitmap->h;

  drect.x = drect.y = 0;
  drect.w = 560; // Standard output width
  drect.h = 384; // Standard output height

  if (g_hDeviceBitmap) {
      VideoSoftStretch(g_hLogoBitmap, &srect, g_hDeviceBitmap, &drect);
  }
}

auto VideoHasRefreshed() -> bool {
  bool result = hasrefreshed;
  hasrefreshed = false;
  return result;
}

void VideoInitialize() {
  static bool mutex_initialized = false;
  if (!mutex_initialized) {
    mutex_initialized = true;
  }

  // CREATE A BUFFER FOR AN IMAGE OF THE LAST DRAWN MEMORY
  vidlastmem.reset(static_cast<uint8_t*>(malloc(0x10000)));
  if (vidlastmem) {
    memset(vidlastmem.get(), 0, 0x10000);
  }

  // LOAD THE splash screen
  g_hLogoBitmap = assets->splash;

  // LOAD APPLE CHARSET40
  if (!charset40) {
    charset40 = LoadCharset();
}

  // Load font_sfc for stretch.h
  if (font_sfc == nullptr) {
    font_sfc = assets->font;
  }

  // CREATE AN IDENTITY PALETTE AND FILL IN THE CORRESPONDING COLORS IN THE BITMAPINFO STRUCTURE
  CreateIdentityPalette();

  // PREFILL THE 16 CUSTOM COLORS AND MAKE SURE TO INCLUDE THE CURRENT MONOCHROME COLOR
  for (int index = DARK_RED; index <= NUM_COLOR_PALETTE; index++) {
    customcolors[index - DARK_RED] = RGB(framebufferinfo[index].r, framebufferinfo[index].g, framebufferinfo[index].b);
}
  // bmiColors
  // CREATE THE FRAME BUFFER DIB SECTION AND DEVICE CONTEXT,
  // CREATE THE SOURCE IMAGE DIB SECTION AND DRAW INTO THE SOURCE BIT BUFFER
  CreateDIBSections();

  // RESET THE VIDEO MODE SWITCHES AND THE CHARACTER SET OFFSET
  VideoResetState();

  // GPH Experiment with multicore video
  if (!g_singlethreaded) {
    VideoInitWorker();
  }
}

// VideoSetNextScheduledUpdate
// Sets
auto video_next_scheduled_update_ = std::chrono::system_clock::now();
void VideoSetNextScheduledUpdate()
{
  if (!g_singlethreaded) {
    //video_next_scheduled_update_ += std::chrono::microseconds(1000); //6666);
    video_next_scheduled_update_ = std::chrono::system_clock::now();
    std::this_thread::yield();
  }
}

// VideoWorkerThread
// Simple polling thread that calls the refresh function
// when necessary.
void VideoWorkerThread()
{
  (void)nullptr;
  std::mutex mtx;
  std::unique_lock<std::mutex> lck(mtx);
  while (!video_worker_terminate_) {
    video_cv.wait_until(lck, video_next_scheduled_update_);
    {
      if (video_worker_refresh_) {
        VideoPerformRefresh();
        video_worker_refresh_ = false;
        std::this_thread::yield();
      }
    }
  }

}

// VideoIniteWorker
// Initializes worker thread for video updates
auto VideoInitWorker() -> bool
{
  video_worker_active_ = true;
  try {
    video_worker_thread_ = std::thread(VideoWorkerThread);
  } catch (...) {
    // If failed to start, revert to singlethreaded
    std::cerr << "FAILED to start video worker; reverting to single-threaded video updating..." << std::endl;
    g_singlethreaded = true;
    video_worker_active_ = false;
  }
  return true;
}

void VideoRealizePalette() {
}

void VideoRedrawScreen() {
  redrawfull = true;
  VideoRefreshScreen();
}

void VideoUpdateOutputBuffer() {
    VideoRect s = {0, 0, 560, 384};
    VideoSurface dst{};
    dst.pixels = reinterpret_cast<uint8_t*>(g_pVideoOutput);
    dst.w = 560;
    dst.h = 384;
    dst.pitch = 560 * 4;
    dst.bpp = 4;

    // Convert internal INDEX8 bitmap to RGB32 output buffer
    VideoSoftStretch(g_hDeviceBitmap, &s, &dst, &s);

    // If status panel is visible, overlay it
    if (g_iStatusCycle > 0 && g_ShowLeds && g_hStatusSurface) {
        VideoRect ss = {0, 0, STATUS_PANEL_W, STATUS_PANEL_H};
        VideoRect ds = {560 - STATUS_PANEL_W - 5, 384 - STATUS_PANEL_H - 5, STATUS_PANEL_W, STATUS_PANEL_H};
        VideoSoftStretch(g_hStatusSurface, &ss, &dst, &ds);
    }
}

void VideoPerformRefresh() {
  g_video_draw_mutex.lock();

  displaypage2_latched = displaypage2;
  vidmode_latched = g_uVideoMode;

  if (g_state.mode == MODE_DEBUG)
  {
    if (redrawfull==0)
    {
        g_video_draw_mutex.unlock();
        return;
    }
    if (g_uDebugVideoMode > 0)
    {
      vidmode_latched = g_uDebugVideoMode;
      displaypage2_latched = (g_uDebugVideoMode & VF_PAGE2)>0;
      g_uDebugVideoMode = 0;
    }
  }

  uint8_t* addr = framebufferbits;
  int pitch = 560;
  CreateFrameOffsetTable(addr, pitch);

  if (g_singlethreaded) {
    g_pHiresBank1 = MemGetAuxPtr(0x2000 << displaypage2_latched);
    g_pHiresBank0 = MemGetMainPtr(0x2000 << displaypage2_latched);
    g_pTextBank1 = MemGetAuxPtr(0x0400 << displaypage2_latched);
    g_pTextBank0 = MemGetMainPtr(0x0400 << displaypage2_latched);
  } else {
    // One-level pipelining to allow CPU emulation to run concurrently without display glitches.
    memcpy(display_pipeline_       ,MemGetAuxPtr ( 0x2000 << displaypage2_latched), 0x2000);
    memcpy(display_pipeline_+0x2000,MemGetMainPtr( 0x2000 << displaypage2_latched), 0x2000);
    memcpy(display_pipeline_+0x4000,MemGetAuxPtr ( 0x0400 << displaypage2_latched), 0x0400);
    memcpy(display_pipeline_+0x4400,MemGetMainPtr( 0x0400 << displaypage2_latched), 0x0400);

    g_pHiresBank1 = reinterpret_cast<uint8_t*>(display_pipeline_);
    g_pHiresBank0 = reinterpret_cast<uint8_t*>(display_pipeline_) + 0x2000;
    g_pTextBank1 =  reinterpret_cast<uint8_t*>(display_pipeline_) + 0x4000;
    g_pTextBank0 =  reinterpret_cast<uint8_t*>(display_pipeline_) + 0x4400;
  }
  memset(celldirty, 0, static_cast<size_t>(40 * 32));
  UpdateFunc_t update = SWL_TEXT ? SWL_80COL ? Update80ColCell : Update40ColCell : SWL_HIRES ? (SWL_DHIRES && SWL_80COL)
                                                                                            ? UpdateDHiResCell
                                                                                            : UpdateHiResCell
                                                                                          : (SWL_DHIRES && SWL_80COL)
                                                                                            ? UpdateDLoResCell
                                                                                            : UpdateLoResCell;

  bool anydirty = redrawfull | g_bTextFlashFlag;

  int y = 0;
  int ypixel = 0;
  while (y < 20) {
    int offset = ((y & 7) << 7) + ((y >> 3) * 40);
    int x = 0;
    int xpixel = 0;
    while (x < 40) {
      anydirty |= celldirty[x][y] = update(x, y, xpixel, ypixel, offset + x);
      ++x;
      xpixel += 14;
    }
    ++y;
    ypixel += 16;
  }

  if (SWL_MIXED) {
    update = SWL_80COL ? Update80ColCell : Update40ColCell;
  }

  while (y < 24) {
    int offset = ((y & 7) << 7) + ((y >> 3) * 40);
    int x = 0;
    int xpixel = 0;
    while (x < 40) {
      anydirty |= celldirty[x][y] = update(x, y, xpixel, ypixel, offset + x);
      ++x;
      xpixel += 14;
    }
    ++y;
    ypixel += 16;
  }

  if (anydirty) {
    g_bTextFlashFlag = false;
  }

  if (g_iStatusCycle > 0) {
    g_iStatusCycle--;
    if (!g_iStatusCycle) {
      Peripheral_Command(7, HARDDISK_CMD_RESET_STATUS, nullptr, 0);
    }
  }

  // Update final output buffer
  VideoUpdateOutputBuffer();

  g_bFrameReady = true;

  SetLastDrawnImage();
  redrawfull = false;
  hasrefreshed = true;

  g_video_draw_mutex.unlock();
}

void VideoReinitialize() {
  CreateIdentityPalette();
  CreateDIBSections();
}

void VideoRefreshScreen( uint32_t uRedrawWholeScreenVideoMode /* =0*/, bool bRedrawWholeScreen /* =false*/ ) {
  // If multithreaded, tell thread to do it; otherwise, do it in this thread
  if (bRedrawWholeScreen)
  {
    g_uDebugVideoMode = uRedrawWholeScreenVideoMode;
    redrawfull = true;
  }
  if (video_worker_active_) {
    video_worker_refresh_ = true;
  } else {
    // If singlethreaded just call the refresh here.
    VideoPerformRefresh();
    hasrefreshed = true;
  }
}

void VideoResetState() {
  g_nAltCharSetOffset = 0;
  displaypage2 = false;
  g_uVideoMode = VF_TEXT;
  redrawfull = true;
}

auto VideoSetMode(uint16_t, uint16_t address, uint8_t write, uint8_t, uint32_t nCyclesLeft) -> uint8_t {
  (void)write;

  // Claim video mutex giving deference to any drawing operation
  // in progress in another thread

  address &= 0xFF;
  int oldvalue = g_nAltCharSetOffset + static_cast<int>(g_uVideoMode & ~(VF_MASK2 | VF_PAGE2));
  switch (address) {
    case 0x00:
      g_uVideoMode &= ~VF_MASK2;
      break;
    case 0x01:
      g_uVideoMode |= VF_MASK2;
      break;
    case 0x0C:
      if (!IS_APPLE2()) {
        g_uVideoMode &= ~VF_80COL;
}
      break;
    case 0x0D:
      if (!IS_APPLE2()) {
        g_uVideoMode |= VF_80COL;
}
      break;
    case 0x0E:
      if (!IS_APPLE2()) {
        g_nAltCharSetOffset = 0;
}
      break;  // Alternate char set off
    case 0x0F:
      if (!IS_APPLE2()) {
        g_nAltCharSetOffset = 256;
}
      break;  // Alternate char set on
    case 0x50:
      g_uVideoMode &= ~VF_TEXT;
      break;
    case 0x51:
      g_uVideoMode |= VF_TEXT;
      break;
    case 0x52:
      g_uVideoMode &= ~VF_MIXED;
      break;
    case 0x53:
      g_uVideoMode |= VF_MIXED;
      break;
    case 0x54:
      g_uVideoMode &= ~VF_PAGE2;
      break;
    case 0x55:
      g_uVideoMode |= VF_PAGE2;
      break;
    case 0x56:
      g_uVideoMode &= ~VF_HIRES;
      break;
    case 0x57:
      g_uVideoMode |= VF_HIRES;
      break;
    case 0x5E:
      if (!IS_APPLE2()) {
        g_uVideoMode |= VF_DHIRES;
}
      break;
    case 0x5F:
      if (!IS_APPLE2()) {
        g_uVideoMode &= ~VF_DHIRES;
}
      break;
  }
  if (SW_MASK2) {
    g_uVideoMode &= ~VF_PAGE2;
}
  if (oldvalue != g_nAltCharSetOffset + static_cast<int>(g_uVideoMode & ~(VF_MASK2 | VF_PAGE2))) {
    graphicsmode = !SW_TEXT;
    redrawfull = true;
    VideoRefreshScreen();
  }

  if (displaypage2 != (SW_PAGE2 != 0)) {
    displaypage2 = (SW_PAGE2 != 0);
    redrawfull = true;
    VideoRefreshScreen();
  }

  return MemReadFloatingBus(nCyclesLeft);
}

static uint32_t g_dwVideoCyclesInFrame = 0;
void VideoUpdateVbl(uint32_t dwCyclesThisFrame) {
  g_dwVideoCyclesInFrame += dwCyclesThisFrame;
  while (g_dwVideoCyclesInFrame >= 17030) {
    g_dwVideoCyclesInFrame -= 17030;
    VideoRefreshScreen();
    VideoUpdateFlash();
  }
}

// Called at 60Hz (every 16.666ms)
void VideoUpdateFlash() {
  static uint32_t nTextFlashCnt = 0;
  nTextFlashCnt++;
  if (nTextFlashCnt == 60 / 6) { // Flash rate = 6Hz (every 166ms)
    nTextFlashCnt = 0;
    g_bTextFlashState = !g_bTextFlashState;

    // Redraw any FLASHing chars if any text showing. NB. No FLASH mode for 80 cols
    if ((SW_TEXT || SW_MIXED)) { // FIX: FLASH 80-Column
      g_bTextFlashFlag = true;
    }
  }
}

auto VideoGetSW80COL() -> bool
{
  return SW_80COL != 0;
}

auto VideoGetSWDHIRES() -> bool
{
  return SW_DHIRES != 0;
}

auto VideoGetSWHIRES() -> bool
{
  return SW_HIRES != 0;
}

auto VideoGetSW80STORE() -> bool
{
  return SW_MASK2 != 0;
}

auto VideoGetSWMIXED() -> bool
{
  return SW_MIXED != 0;
}

auto VideoGetSWPAGE2() -> bool
{
  return SW_PAGE2 != 0;
}

auto VideoGetSWTEXT() -> bool
{
  return SW_TEXT != 0;
}

auto VideoGetSWAltCharSet() -> bool
{
  return g_nAltCharSetOffset != 0;
}

//===========================================================================
auto VideoGetSnapshot(SS_IO_Video *pSS) -> uint32_t {
  pSS->bAltCharSet = g_nAltCharSetOffset != 0;
  pSS->dwVidMode = g_uVideoMode;
  return 0;
}

auto VideoSetSnapshot(SS_IO_Video *pSS) -> uint32_t {
  g_nAltCharSetOffset = !pSS->bAltCharSet ? 0 : 256;
  g_uVideoMode = pSS->dwVidMode;

  graphicsmode = !SW_TEXT;
  displaypage2 = (SW_PAGE2 != 0);

  return 0;
}

auto VideoGetScannerAddress(bool *pbVblBar_OUT, const uint32_t uExecutedCycles) -> uint16_t {
  // get video scanner position
  int nCycles = (g_dwVideoCyclesInFrame + uExecutedCycles) % 17030;

  // machine state switches
  int nHires = (SW_HIRES & !SW_TEXT) ? 1 : 0;
  int nPage2 = (SW_PAGE2) ? 1 : 0;
  int n80Store = (MemGet80Store()) ? 1 : 0;

  // calculate video parameters according to display standard
  int nScanLines = g_state.bVideoScannerNTSC ? kNTSCScanLines : kPALScanLines;

  // calculate horizontal scanning state
  int nHClock = (nCycles + kHPEClock) % kHClocks; // which horizontal scanning clock
  int nHState = kHClock0State + nHClock; // H state bits
  if (nHClock >= kHPresetClock) { // check for horizontal preset
    nHState -= 1; // correct for state preset (two 0 states)
  }
  int h_0 = (nHState >> 0) & 1; // get horizontal state bits
  int h_1 = (nHState >> 1) & 1;
  int h_2 = (nHState >> 2) & 1;
  int h_3 = (nHState >> 3) & 1;
  int h_4 = (nHState >> 4) & 1;
  int h_5 = (nHState >> 5) & 1;

  // calculate vertical scanning state
  int nVLine = nCycles / kHClocks; // which vertical scanning line
  int nVState = kVLine0State + nVLine; // V state bits
  if ((nVLine >= kVPresetLine)) { // check for previous vertical state preset
    nVState -= nScanLines; // compensate for preset
  }
  int v_A = (nVState >> 0) & 1; // get vertical state bits
  int v_B = (nVState >> 1) & 1;
  int v_C = (nVState >> 2) & 1;
  int v_0 = (nVState >> 3) & 1;
  int v_1 = (nVState >> 4) & 1;
  int v_2 = (nVState >> 5) & 1;
  int v_3 = (nVState >> 6) & 1;
  int v_4 = (nVState >> 7) & 1;

  // calculate scanning memory address
  if (SW_HIRES && SW_MIXED && (v_4 & v_2)) {
    // The softswitch for this is $c053 for mixed, $c052 for fill (no text on bottom).
    nHires = 0; // (address is in text memory)
  }

  int nAddend0 = 0x68; // 1            1            0            1
  int nAddend1 = (h_5 << 5) | (h_4 << 4) | (h_3 << 3);
  int nAddend2 = (v_4 << 6) | (v_3 << 5) | (v_4 << 4) | (v_3 << 3);
  int nSum = (nAddend0 + nAddend1 + nAddend2) & (0x0F << 3);

  int nAddress = 0;
  nAddress |= h_0 << 0; // a0
  nAddress |= h_1 << 1; // a1
  nAddress |= h_2 << 2; // a2
  nAddress |= nSum;     // a3 - aa6
  nAddress |= v_0 << 7; // a7
  nAddress |= v_1 << 8; // a8
  nAddress |= v_2 << 9; // a9
  nAddress |= ((nHires) ? v_A : (1 ^ (nPage2 & (1 ^ n80Store)))) << 10; // a10
  nAddress |= ((nHires) ? v_B : (nPage2 & (1 ^ n80Store))) << 11; // a11
  if (nHires) { // hires?
    // Y: insert hires only address bits
    nAddress |= v_C << 12; // a12
    nAddress |= (1 ^ (nPage2 & (1 ^ n80Store))) << 13; // a13
    nAddress |= (nPage2 & (1 ^ n80Store)) << 14; // a14
  } else {
    // N: text, so no higher address bits unless Apple ][, not Apple //e
    if ((IS_APPLE2()) && // Apple ][?
        (kHPEClock <= nHClock) && // Y: HBL?
        (nHClock <= (kHClocks - 1))) {
      nAddress |= 1 << 12; // Y: a12 (add $1000 to address!)
    }
  }

  if (pbVblBar_OUT != nullptr) {
    if (v_4 & v_3) { // VBL?
      *pbVblBar_OUT = true; // Y: VBL is true
    } else {
      *pbVblBar_OUT = false; // N: VBL is false
    }
  }
  return static_cast<uint16_t>(nAddress);
}

auto VideoGetVbl(const uint32_t uExecutedCycles) -> bool {
  // get cycles within current frame
  int nCycles = (g_dwVideoCyclesInFrame + uExecutedCycles) % 17030;

  // Apple II NTSC: 262 lines, 65 cycles per line.
  // Visible area: lines 0-191. VBL: lines 192-261.
  // VBL starts at cycle 192 * 65 = 12480.

  return (nCycles >= 12480);
}
