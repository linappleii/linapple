// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/Video.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

#include "apple2/Apple2Types.h"
#include "core/Asset.h"
#include "core/LinAppleCore.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-constant-array-index,
// bugprone-easily-swappable-parameters, bugprone-branch-clone,
// bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions,
// bugprone-misplaced-widening-cast, bugprone-switch-missing-default-case,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-avoid-do-while, cppcoreguidelines-init-variables,
// cppcoreguidelines-macro-usage, cppcoreguidelines-no-malloc,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-type-reinterpret-cast,
// cppcoreguidelines-use-enum-class, google-readability-casting,
// modernize-avoid-c-style-cast): Unavoidable hardware architectural constraints
// for Apple II CRT rendering, NTSC/PAL timing generator, and video memory
// scanner
static auto GetTickCount() -> uint32_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/harddisk/Harddisk.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "charset40.xpm"
#include "charset40_IIplus.xpm"
#include "charset40_british.xpm"
#include "charset40_french.xpm"
#include "charset40_german.xpm"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/VideoStretch.h"

static uint32_t g_video_output[VIDEO_WIDTH * VIDEO_HEIGHT] = {};
static bool s_language_rocker_switch = false;

auto video_get_output_buffer() -> uint32_t* { return g_video_output; }

#define GetRValue(rgb) ((uint8_t)(rgb))
#define GetGValue(rgb) ((uint8_t)(((uint16_t)(rgb)) >> 8))
#define GetBValue(rgb) ((uint8_t)((rgb) >> 16))
#define FLASH_80_COL 1

const int SRCOFFS_40COL = 0;
const int SRCOFFS_80COL = (SRCOFFS_40COL + 256);
const int SRCOFFS_LORES = (SRCOFFS_80COL + 128);
const int SRCOFFS_HIRES = (SRCOFFS_LORES + 16);
const int SRCOFFS_DHIRES = (SRCOFFS_HIRES + 512);
const int SRCOFFS_TOTAL = (SRCOFFS_DHIRES + 2560);

#define SW_80COL (g_video_mode & VF_80COL)
#define SW_DHIRES (g_video_mode & VF_DHIRES)
#define SW_HIRES (g_video_mode & VF_HIRES)
#define SW_MASK2 (g_video_mode & VF_MASK2)
#define SW_MIXED (g_video_mode & VF_MIXED)
#define SW_PAGE2 (g_video_mode & VF_PAGE2)
#define SW_TEXT (g_video_mode & VF_TEXT)

#define SWL_80COL (vidmode_latched & VF_80COL)
#define SWL_DHIRES (vidmode_latched & VF_DHIRES)
#define SWL_HIRES (vidmode_latched & VF_HIRES)
#define SWL_MASK2 (vidmode_latched & VF_MASK2)
#define SWL_MIXED (vidmode_latched & VF_MIXED)
#define SWL_PAGE2 (vidmode_latched & VF_PAGE2)
#define SWL_TEXT (vidmode_latched & VF_TEXT)

#define SOFTSTRECH(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, DST_W, \
                   DST_H)                                                     \
  {                                                                           \
    VideoRect_t srcrect = {SRC_X, SRC_Y, SRC_W, SRC_H};                       \
    VideoRect_t dstrect = {DST_X, DST_Y, DST_W, DST_H};                       \
    video_soft_stretch(SRC, &srcrect, DST, &dstrect);                         \
  }

#define SOFTSTRECH_MONO(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, \
                        DST_W, DST_H)                                       \
  {                                                                         \
    VideoRect_t srcrect = {SRC_X, SRC_Y, SRC_W, SRC_H};                     \
    VideoRect_t dstrect = {DST_X, DST_Y, DST_W, DST_H};                     \
    video_soft_stretch_mono8(SRC, &srcrect, DST, &dstrect, hBrush, 0);      \
  }

#define SETSOURCEPIXEL(x, y, c) g_source_start_of_line[(y)][(x)] = (c)
#define SETFRAMECOLOR(i, r1, g1, b1) \
  framebufferinfo[i].r = r1;         \
  framebufferinfo[i].g = g1;         \
  framebufferinfo[i].b = b1;

// video scanner constants
int const kHClock0State = 0x18;  // H[543210] = 011000
int const kHClocks = 65;         // clocks per horizontal scan (including HBL)
int const kHPEClock = 40;  // clock when HPE (horizontal preset enable) goes low
int const kHPresetClock = 41;    // clock when H state presets
int const kNTSCScanLines = 262;  // total scan lines including VBL (NTSC)
int const kPALScanLines = 312;   // total scan lines including VBL (PAL)
int const kVLine0State = 0x100;  // V[543210CBA] = 100000000
int const kVPresetLine = 256;    // line when V state presets

using UpdateFunc_t = bool (*)(int, int, int, int, int);

static uint8_t celldirty[TEXT_COLUMNS][DIRTY_CELL_ROWS] = {};
static uint32_t customcolors[NUM_COLOR_PALETTE] =
    {};  // MONOCHROME is last custom color

VideoSurface_t* g_device_bitmap;
static uint8_t* framebufferbits;
VideoColor_t framebufferinfo[MAX_PALETTE_SIZE] = {};

auto video_get_output_palette() -> VideoColor_t* { return framebufferinfo; }

static uint8_t* frameoffsettable[VIDEO_HEIGHT] = {};
static uint8_t* g_hires_bank1;
static uint8_t* g_hires_bank0;

VideoSurface_t* g_logo_bitmap = nullptr;
VideoSurface_t* charset40 = nullptr;
int multi_language_charset = false;

VideoSurface_t* g_status_surface = nullptr;
int g_status_cycle = 0;

VideoSurface_t* g_origscreen = nullptr;
VideoSurface_t* g_source_bitmap = nullptr;

static uint8_t* g_source_pixels;
VideoColor_t g_source_header[MAX_PALETTE_SIZE] = {};
const int MAX_SOURCE_Y = 512 * 2;
static uint8_t* g_source_start_of_line[MAX_SOURCE_Y] = {};
static uint8_t* g_text_bank1;
static uint8_t* g_text_bank0;

static uint8_t hgrpixelmatrix[APPLE2_VISIBLE_WIDTH]
                             [APPLE2_VISIBLE_HEIGHT + 2 * HGR_MATRIX_YOFFSET] =
                                 {};
static uint8_t colormixbuffer[6] = {};
static uint16_t colormixmap[6][6][6] = {};

static int g_alt_char_set_offset = 0;
static bool displaypage2 = false;
static bool displaypage2_latched = false;
static uint8_t* framebufferaddr = (uint8_t*)nullptr;
static int framebufferpitch = 0;
bool graphicsmode = false;
static volatile bool hasrefreshed = false;
static uint32_t lastpageflip = 0;
uint32_t monochrome =
    RGB(DEFAULT_GRAY_COMPONENT, DEFAULT_GRAY_COMPONENT, DEFAULT_GRAY_COMPONENT);
static bool redrawfull = true;
static std::unique_ptr<uint8_t[], void (*)(void*)> vidlastmem(nullptr, free);
uint32_t g_video_mode = VF_TEXT;
uint32_t g_debug_video_mode = VF_TEXT;
static uint32_t vidmode_latched = VF_TEXT;
uint32_t g_videotype = VT_COLOR_STANDARD;
uint32_t g_singlethreaded = 1;
std::atomic<bool> g_frame_ready(false);

static bool g_text_flash_state = false;
static bool g_text_flash_flag = false;

bool g_show_leds = true;

auto DrawDHiResSource() -> void;
auto DrawHiResSource() -> void;
auto DrawHiResSourceHalfShiftFull() -> void;
auto DrawHiResSourceHalfShiftDim() -> void;
auto DrawLoResSource() -> void;
auto DrawMonoDHiResSource() -> void;
auto DrawMonoHiResSource() -> void;
auto DrawMonoLoResSource() -> void;
auto DrawMonoTextSource(VideoSurface_t* dc) -> void;
auto DrawTextSource(VideoSurface_t* dc) -> void;
auto LoadCharset() -> VideoSurface_t*;

auto VideoInitWorker() -> bool;

std::thread video_worker_thread_;
static std::atomic<bool> video_worker_active_{false};
static std::atomic<bool> video_worker_terminate_{false};
static std::atomic<bool> video_worker_refresh_{false};
static std::mutex s_video_worker_mutex;
std::recursive_mutex g_video_draw_mutex;
std::condition_variable video_cv;

static char display_pipeline_[0x2000 * 4 + 0x400 * 4] = {};

auto CopySource(int destx, int desty, int xsize, int ysize, int sourcex,
                int sourcey) -> void {
  uint8_t* currdestptr = frameoffsettable[desty] + destx;
  uint8_t* currsourceptr = g_source_start_of_line[sourcey] + sourcex;
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

  for (uint32_t loop = 0; loop < VIDEO_HEIGHT; loop++) {
    frameoffsettable[loop] =
        framebufferaddr + static_cast<ptrdiff_t>(framebufferpitch * loop);
  }
}

void CreateIdentityPalette() {
  memset(framebufferinfo, 0, MAX_PALETTE_SIZE * sizeof(VideoColor_t));
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
  SETFRAMECOLOR(DEEP_SKY_BLUE, 0, 64, 128);
  SETFRAMECOLOR(DARKER_CYAN, 0, 63, 63);
  SETFRAMECOLOR(DARKEST_CYAN, 0, 31, 31);
  SETFRAMECOLOR(HALF_ORANGE, 128, 64, 0);
  SETFRAMECOLOR(DARKER_BLUE, 0x00, 0x00, 63);
  SETFRAMECOLOR(DARKER_YELLOW, 0x00, 63, 63);
  SETFRAMECOLOR(DARKEST_YELLOW, 0x00, 31, 31);
  SETFRAMECOLOR(LIGHTEST_GRAY, 223, 223, 223);
  SETFRAMECOLOR(DARKER_GREEN, 0x00, 63, 0x00);
  SETFRAMECOLOR(DARKEST_GREEN, 0x00, 31, 0x00);
}

void CreateDIBSections() {
  g_video_draw_mutex.lock();

  memcpy(g_source_header, framebufferinfo,
         MAX_PALETTE_SIZE * sizeof(VideoColor_t));

  if (g_device_bitmap) {
    video_destroy_surface(g_device_bitmap);
  }
  g_device_bitmap = video_create_surface(VIDEO_WIDTH, VIDEO_HEIGHT, 1);

  if (g_origscreen) {
    video_destroy_surface(g_origscreen);
  }
  g_origscreen =
      video_create_surface(static_cast<int>(g_state.screen_width),
                           static_cast<int>(g_state.screen_height), 1);

  if (g_device_bitmap == nullptr || g_origscreen == nullptr) {
    fprintf(stderr, "g_device_bitmap or g_origscreen was not created\n");
    g_video_draw_mutex.unlock();
    return;
  }

  framebufferbits = g_device_bitmap->pixels;
  memcpy(g_device_bitmap->palette, g_source_header,
         MAX_PALETTE_SIZE * sizeof(VideoColor_t));
  memcpy(g_origscreen->palette, g_source_header,
         MAX_PALETTE_SIZE * sizeof(VideoColor_t));

  if (g_status_surface) {
    video_destroy_surface(g_status_surface);
  }
  g_status_surface = video_create_surface(STATUS_PANEL_W, STATUS_PANEL_H, 1);
  if (g_status_surface == nullptr) {
    fprintf(stderr, "g_status_surface was not created\n");
    g_video_draw_mutex.unlock();
    return;
  }
  memcpy(g_status_surface->palette, g_source_header,
         MAX_PALETTE_SIZE * sizeof(VideoColor_t));

  VideoRect_t srect{};
  uint8_t mybluez = DARK_BLUE;
  uint8_t myyell = YELLOW;

  srect.x = srect.y = 0;
  srect.w = STATUS_PANEL_W;
  srect.h = STATUS_PANEL_H;
  memset(g_status_surface->pixels, mybluez,
         static_cast<size_t>(STATUS_PANEL_W * STATUS_PANEL_H));
  rectangle(g_status_surface, 0, 0, STATUS_PANEL_W - 1, STATUS_PANEL_H - 1,
            myyell);
  rectangle(g_status_surface, 2, 2, STATUS_PANEL_W - 5, STATUS_PANEL_H - 5,
            myyell);
  if (font_sfc == nullptr) {
    fonts_initialization();
  }
  if (font_sfc != nullptr) {
    const float scale_x = 1.3f;
    const float scale_y = 1.5f;
    const int text_y = 6;
    font_print(7, text_y, "FDD1", g_status_surface, scale_x, scale_y);
    font_print(40, text_y, "FDD2", g_status_surface, scale_x, scale_y);
    font_print(74, text_y, "HDD", g_status_surface, scale_x, scale_y);
  }
  if (g_source_bitmap) {
    video_destroy_surface(g_source_bitmap);
  }
  g_source_bitmap = video_create_surface(SRCOFFS_TOTAL, MAX_SOURCE_Y, 1);
  if (g_source_bitmap == nullptr) {
    fprintf(stderr, "g_source_bitmap was not created\n");
    g_video_draw_mutex.unlock();
    return;
  }

  g_source_pixels = g_source_bitmap->pixels;
  memcpy(g_source_bitmap->palette, framebufferinfo, 256 * sizeof(VideoColor_t));

  for (int y = 0; y < MAX_SOURCE_Y; y++) {
    g_source_start_of_line[y] =
        g_source_pixels + static_cast<ptrdiff_t>(SRCOFFS_TOTAL * y);
  }

  memset(g_source_pixels, 0, static_cast<size_t>(SRCOFFS_TOTAL * MAX_SOURCE_Y));

  if (charset40 == nullptr) {
    charset40 = LoadCharset();
  }

  if ((g_videotype != VT_MONO_CUSTOM) && (g_videotype != VT_MONO_AMBER) &&
      (g_videotype != VT_MONO_GREEN) && (g_videotype != VT_MONO_WHITE)) {
    DrawTextSource(g_source_bitmap);

    DrawLoResSource();
    if (g_videotype == VT_COLOR_HALF_SHIFT_DIM) {
      DrawHiResSourceHalfShiftDim();
    } else {
      DrawHiResSource();
    }
    DrawDHiResSource();
  } else {
    DrawMonoTextSource(g_source_bitmap);

    DrawMonoLoResSource();
    DrawMonoHiResSource();
    DrawMonoDHiResSource();
  }

  g_video_draw_mutex.unlock();
}

void DrawDHiResSource() {
  uint8_t colorval[16] = {BLACK,    DARK_BLUE,  DARK_GREEN, BLUE,
                          BROWN,    LIGHT_GRAY, GREEN,      AQUA,
                          DEEP_RED, MAGENTA,    DARK_GRAY,  LIGHT_BLUE,
                          ORANGE,   PINK,       YELLOW,     WHITE};

  constexpr int OFFSET = 3;
  constexpr int SIZE = 10;
  for (int column = 0; column < 256; column++) {
    int coloffs = SIZE * column;
    for (unsigned byteval = 0; byteval < 256; byteval++) {
      int color[SIZE] = {};
      memset(color, 0, sizeof(color));
      unsigned pattern = (static_cast<uint16_t>(
          (static_cast<uint8_t>(byteval)) |
          (static_cast<uint16_t>(static_cast<uint8_t>(column))) << 8));
      int pixel = 0;
      for (pixel = 1; pixel < 15; pixel++) {
        if (pattern & (1 << pixel)) {
          int pixelcolor = 1 << ((pixel - OFFSET) & 3);
          if ((pixel >= OFFSET + 2) && (pixel < SIZE + OFFSET + 2) &&
              (pattern & (0x7 << (pixel - 4)))) {
            color[pixel - (OFFSET + 2)] |= pixelcolor;
          }
          if ((pixel >= OFFSET + 1) && (pixel < SIZE + OFFSET + 1) &&
              (pattern & (0xF << (pixel - 4)))) {
            color[pixel - (OFFSET + 1)] |= pixelcolor;
          }
          if ((pixel >= OFFSET + 0) && (pixel < SIZE + OFFSET + 0)) {
            color[pixel - (OFFSET + 0)] |= pixelcolor;
          }
          if ((pixel >= OFFSET - 1) && (pixel < SIZE + OFFSET - 1) &&
              (pattern & (0xF << (pixel + 1)))) {
            color[pixel - (OFFSET - 1)] |= pixelcolor;
          }
          if ((pixel >= OFFSET - 2) && (pixel < SIZE + OFFSET - 2) &&
              (pattern & (0x7 << (pixel + 2)))) {
            color[pixel - (OFFSET - 2)] |= pixelcolor;
          }
        }
      }

      if (g_videotype == VT_COLOR_TEXT_OPTIMIZED) {
        // Activate for fringe reduction on white hgr text
        // drawback: loss of color mix patterns in hgr mode.
        // select g_videotype by index

        for (pixel = 0; pixel < 13; pixel++) {
          if ((pattern & (0xF << pixel)) ==
              static_cast<unsigned>(0xF << pixel)) {
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
  CM_Magenta,
  CM_Blue,
  CM_Green,
  CM_Orange,
  CM_Black,
  CM_White,
  NUM_COLOR_MAPPING
};

const uint8_t aColorIndex[NUM_COLOR_MAPPING] = {
    HGR_MAGENTA, HGR_BLUE, HGR_GREEN, HGR_RED, HGR_BLACK, HGR_WHITE};

void DrawHiResSourceHalfShiftDim() {
  for (int column = 0; column < 16; column++) {
    int coloffs = column << 5;

    for (unsigned byte = 0; byte < 256; byte++) {
      int aPixels[11] = {};

      aPixels[0] = column & 4;
      aPixels[1] = column & 8;
      aPixels[9] = column & 1;
      aPixels[10] = column & 2;

      int bit_mask = 1;
      int pixel = 0;
      for (pixel = 2; pixel < 9; pixel++) {
        aPixels[pixel] = ((byte & bit_mask) != 0);
        bit_mask <<= 1;
      }

      int hibit = ((byte & 0x80) != 0);
      int x = 0;
      int y = byte << 1;

      while (x < 28) {
        int adj = (x >= 14) << 1;
        int odd = (x >= 14);

        for (pixel = 2; pixel < 9; pixel++) {
          int color = CM_Black;
          if (aPixels[pixel]) {
            if (aPixels[pixel - 1] || aPixels[pixel + 1]) {
              color = CM_White;
            } else {
              color = ((odd ^ (pixel & 1)) << 1) | hibit;
            }
          } else if (aPixels[pixel - 1] && aPixels[pixel + 1]) {
            // Activate for fringe reduction on white hgr text -
            // drawback: loss of color mix patterns in hgr mode.
            // select g_videotype by index exclusion
            if (!(aPixels[pixel - 2] && aPixels[pixel + 2])) {
              color = ((odd ^ !(pixel & 1)) << 1) | hibit;
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
*/
          switch (color) {
            case CM_Magenta:
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, HGR_MAGENTA);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y,
                             DARK_MAGENTA);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1,
                             HGR_MAGENTA);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1,
                             DARK_MAGENTA);
              break;
            case CM_Blue:
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y,
                             HGR_BLUE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 2, y,
                             DARK_BLUE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1,
                             HGR_BLUE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 2, y + 1,
                             DARK_BLUE);
              if (hibit) {
                if (pixel <= 2) {
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y,
                                 DARK_BLUE);
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1,
                                 DARK_BLUE);
                }
              }
              break;
            case CM_Green:
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, HGR_GREEN);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y,
                             DARK_GREEN);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1,
                             HGR_GREEN);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1,
                             DARK_GREEN);
              break;
            case CM_Orange:
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y, HGR_RED);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 2, y, BROWN);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1,
                             HGR_RED);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 2, y + 1,
                             BROWN);
              if (hibit) {
                if (pixel <= 2) {
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, BROWN);
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1,
                                 BROWN);
                }
              }
              break;
            case CM_Black:
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, HGR_BLACK);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y,
                             HGR_BLACK);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1,
                             HGR_BLACK);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1,
                             HGR_BLACK);
              break;
            case CM_White:
              // Maintain solid white pixel rendering without half-shift
              // dithering
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y, HGR_WHITE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y,
                             HGR_WHITE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1,
                             HGR_WHITE);
              SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1,
                             HGR_WHITE);
              if (hibit) {
                if (pixel <= 2) {
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y,
                                 HGR_WHITE);
                  SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1,
                                 HGR_WHITE);
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
  for (int column = 0; column < 16; column++) {
    int coloffs = column << 5;

    for (unsigned byte = 0; byte < 256; byte++) {
      int aPixels[11] = {};

      aPixels[0] = column & 4;
      aPixels[1] = column & 8;
      aPixels[9] = column & 1;
      aPixels[10] = column & 2;

      int bit_mask = 1;
      int pixel = 0;
      for (pixel = 2; pixel < 9; pixel++) {
        aPixels[pixel] = ((byte & bit_mask) != 0);
        bit_mask <<= 1;
      }

      int hibit = ((byte & 0x80) != 0);
      int x = 0;
      int y = byte << 1;

      while (x < 28) {
        int adj = (x >= 14) << 1;
        int odd = (x >= 14);

        for (pixel = 2; pixel < 9; pixel++) {
          int color = CM_Black;
          if (aPixels[pixel]) {
            if (aPixels[pixel - 1] || aPixels[pixel + 1]) {
              color = CM_White;
            } else {
              color = ((odd ^ (pixel & 1)) << 1) | hibit;
            }
          } else if (aPixels[pixel - 1] && aPixels[pixel + 1]) {
            if ((g_videotype == VT_COLOR_STANDARD) ||
                (g_videotype == VT_COLOR_TVEMU) ||
                !(aPixels[pixel - 2] && aPixels[pixel + 2])) {
              color = ((odd ^ !(pixel & 1)) << 1) | hibit;
            }
          }

          SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y,
                         aColorIndex[color]);
          SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y,
                         aColorIndex[color]);

          if (VT_COLOR_TVEMU > g_videotype) {
            SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1,
                           aColorIndex[color]);
            SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1,
                           aColorIndex[color]);
          } else {
            SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj, y + 1,
                           aColorIndex[0]);
            SETSOURCEPIXEL(SRCOFFS_HIRES + coloffs + x + adj + 1, y + 1,
                           aColorIndex[0]);
          }

          x += 2;
        }
      }
    }
  }
}

void DrawLoResSource() {
  uint8_t colorval[16] = {BLACK,      DEEP_RED,  DARK_BLUE,  MAGENTA,
                          DARK_GREEN, DARK_GRAY, BLUE,       LIGHT_BLUE,
                          BROWN,      ORANGE,    LIGHT_GRAY, PINK,
                          GREEN,      YELLOW,    AQUA,       WHITE};
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
    default:
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
      unsigned pattern = (static_cast<uint16_t>(
          (static_cast<uint8_t>(byteval)) |
          (static_cast<uint16_t>(static_cast<uint8_t>(column))) << 8));
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

void DrawMonoTextSource(VideoSurface_t* hDstDC) {
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
    default:
      hBrush = MONOCHROME_CUSTOM;
      break;
  }

  if ((g_apple2_type == A2TYPE_APPLE2) ||
      (g_apple2_type == A2TYPE_APPLE2PLUS)) {
    SOFTSTRECH_MONO(charset40, 0, 0, 128, 128, hDstDC, SRCOFFS_40COL, 0, 256,
                    256);
  } else {
    int MaxLanguage = (multi_language_charset) ? 2 : 1;
    for (int Language = 0; Language < MaxLanguage; Language++) {
      /* When ROM contains two character sets: US/default set is the second
       * (starting at offset 128), while the local language set is always the
       * first (offset 0). */
      int srcYofs = ((Language == 0) && (multi_language_charset)) ? 128 : 0;
      int dstYofs = Language * (MAX_SOURCE_Y / 2);

      SOFTSTRECH_MONO(charset40, 0, srcYofs, 128, 128, hDstDC, SRCOFFS_40COL,
                      dstYofs, 256, 256);
      SOFTSTRECH_MONO(hDstDC, 0, dstYofs, 256, 256, hDstDC, SRCOFFS_40COL,
                      256 + dstYofs, 256, 256);
      SOFTSTRECH_MONO(hDstDC, 0, dstYofs, 256, 64, hDstDC, SRCOFFS_40COL,
                      64 + dstYofs, 256, 64);

      if (g_apple2_type == A2TYPE_APPLE2E) {
        SOFTSTRECH_MONO(hDstDC, 0, 256 + dstYofs, 256, 32, hDstDC,
                        SRCOFFS_40COL, 256 + 64 + dstYofs, 256, 32);
      }
    }

    SOFTSTRECH_MONO(hDstDC, 0, 0, 256, MAX_SOURCE_Y, hDstDC, SRCOFFS_80COL, 0,
                    128, MAX_SOURCE_Y);
  }
}

void DrawTextSource(VideoSurface_t* dc) {
  if (charset40 == nullptr) {
    return;
  }
  uint8_t hBrush = GetMonochromeIndex();

  if ((g_apple2_type == A2TYPE_APPLE2) ||
      (g_apple2_type == A2TYPE_APPLE2PLUS)) {
    SOFTSTRECH_MONO(charset40, 0, 0, 128, 128, dc, SRCOFFS_40COL, 0, 256, 256);
  } else {
    int MaxLanguage = (multi_language_charset) ? 2 : 1;
    for (int Language = 0; Language < MaxLanguage; Language++) {
      /* When ROM contains two character sets: US/default set is the second
       * (starting at offset 128), while the local language set is always the
       * first (offset 0). */
      int srcYofs = ((Language == 0) && (multi_language_charset)) ? 128 : 0;
      int dstYofs = Language * (MAX_SOURCE_Y / 2);

      SOFTSTRECH_MONO(charset40, 0, srcYofs, 128, 128, dc, SRCOFFS_40COL,
                      dstYofs, 256, 256);
      SOFTSTRECH_MONO(dc, 0, dstYofs, 256, 256, dc, SRCOFFS_40COL,
                      256 + dstYofs, 256, 256);
      SOFTSTRECH_MONO(dc, 0, dstYofs, 256, 64, dc, SRCOFFS_40COL, 64 + dstYofs,
                      256, 64);

      if (g_apple2_type == A2TYPE_APPLE2E) {
        SOFTSTRECH_MONO(dc, 0, 256 + dstYofs, 256, 32, dc, SRCOFFS_40COL,
                        256 + 64 + dstYofs, 256, 32);
      }
    }

    SOFTSTRECH_MONO(dc, 0, 0, 256, MAX_SOURCE_Y, dc, SRCOFFS_80COL, 0, 128,
                    MAX_SOURCE_Y);
  }
}

void SetLastDrawnImage() {
  if (vidlastmem == nullptr) {
    return;
  }
  memcpy(vidlastmem.get() + 0x400, g_text_bank0, 0x400);
  if (SWL_HIRES) {
    memcpy(vidlastmem.get() + 0x2000, g_hires_bank0, 0x2000);
  }
  if (SWL_DHIRES && SWL_HIRES) {
    memcpy(vidlastmem.get(), g_hires_bank1, 0x2000);
  } else if (SWL_80COL) {  // Don't test for !SWL_HIRES, as some 80-col text
                           // routines have SWL_HIRES set
    memcpy(vidlastmem.get(), g_text_bank1, 0x400);
  }
  int loop = 0;
  for (loop = 0; loop < 256; loop++) {
    *(memdirty + loop) &= ~2;
  }
}

// These "Update" functions update the SDL graphics buffer to be
// displayed on the host with what the "Draw" functions have
// drawn into the guest Apple graphics buffers.

auto Update40ColCell(int x, int y, int xpixel, int ypixel, int offset) -> bool {
  if (!vidlastmem) return false;
  (void)x;
  (void)y;
  uint8_t ch = *(g_text_bank0 + offset);
  bool char_changed = (ch != *(vidlastmem.get() + offset + 0x400) ||
                       redrawfull || video_worker_active_);

  // FLASHing chars:
  // - FLASHing if:Alt Char Set is OFF && 0x40<=char<=0x7F
  // - The inverse of this char is located at: char+0x40
  bool char_flashing =
      (g_alt_char_set_offset == 0) && (ch >= 0x40) && (ch <= 0x7F);

  if (char_changed || (char_flashing && g_text_flash_flag)) {
    bool invert = char_flashing ? g_text_flash_state : false;

    CopySource(
        xpixel, ypixel, APPLE_FONT_WIDTH, APPLE_FONT_HEIGHT,
        SRCOFFS_40COL + ((ch & 0x0F) << 4),
        (ch & 0xF0) + g_alt_char_set_offset + (invert ? 0x40 : 0x00) +
            ((s_language_rocker_switch && multi_language_charset) ? 512 : 0));
    return true;
  }
  return false;
}

inline auto Update80ColumnCell(uint8_t c, const int xPixel, const int yPixel,
                               bool char_flashing) -> bool {
  bool invert = char_flashing ? g_text_flash_state : false;
  CopySource(
      xPixel, yPixel, (APPLE_FONT_WIDTH / 2), APPLE_FONT_HEIGHT,
      SRCOFFS_80COL + ((c & 15) << 3),
      ((c >> 4) << 4) + g_alt_char_set_offset + (invert ? 0x40 : 0x00) +
          ((s_language_rocker_switch && multi_language_charset) ? 512 : 0));
  return true;
}

auto Update80ColCell(int x, int y, int xpixel, int ypixel, int offset) -> bool {
  if (!vidlastmem) return false;
  (void)x;
  (void)y;
  (void)xpixel;
  (void)ypixel;
  (void)offset;
  bool dirty = false;

#if FLASH_80_COL
  uint8_t c1 = *(g_text_bank1 + offset);
  uint8_t c0 = *(g_text_bank0 + offset);

  bool c1_changed = (c1 != *(vidlastmem.get() + offset + 0) || redrawfull ||
                     video_worker_active_);
  bool c0_changed = (c0 != *(vidlastmem.get() + offset + 0x400) || redrawfull ||
                     video_worker_active_);

  bool c1_flashing =
      (g_alt_char_set_offset == 0) && (c1 >= 0x40) && (c1 <= 0x7F);
  bool c0_flashing =
      (g_alt_char_set_offset == 0) && (c0 >= 0x40) && (c0 <= 0x7F);

  if (c1_changed || (c1_flashing && g_text_flash_flag)) {
    dirty = Update80ColumnCell(c1, xpixel, ypixel, c1_flashing);
  }

  if (c0_changed || (c0_flashing && g_text_flash_flag)) {
    dirty |= Update80ColumnCell(c0, xpixel + 7, ypixel, c0_flashing);
  }
#endif

  return dirty;
}

auto UpdateDHiResCell(int x, int y, int xpixel, int ypixel, int offset)
    -> bool {
  if (!vidlastmem) return false;
  (void)y;
  bool dirty = false;
  int yoffset = 0;
  while (yoffset < 0x2000) {
    uint8_t byteval1 = (x > 0) ? *(g_hires_bank0 + offset + yoffset - 1) : 0;
    uint8_t byteval2 = *(g_hires_bank1 + offset + yoffset);
    uint8_t byteval3 = *(g_hires_bank0 + offset + yoffset);
    uint8_t byteval4 = (x < 39) ? *(g_hires_bank1 + offset + yoffset + 1) : 0;
    if ((byteval2 != *(vidlastmem.get() + offset + yoffset)) ||
        (byteval3 != *(vidlastmem.get() + offset + yoffset + 0x2000)) ||
        ((x > 0) &&
         ((byteval1 & 0x70) !=
          (*(vidlastmem.get() + offset + yoffset + 0x1FFF) & 0x70))) ||
        ((x < 39) && ((byteval4 & 0x07) !=
                      (*(vidlastmem.get() + offset + yoffset + 1) & 0x07))) ||
        redrawfull || video_worker_active_) {
      uint32_t dwordval = (byteval1 & 0x70) | ((byteval2 & 0x7F) << 7) |
                          ((byteval3 & 0x7F) << 14) | ((byteval4 & 0x07) << 21);
      {
        constexpr int PIXEL = 0;
#define COLOR ((xpixel + PIXEL) & 3)
#define VALUE (dwordval >> (4 + PIXEL - COLOR))
        CopySource(xpixel + PIXEL, ypixel + (yoffset >> 9), 7, 2,
                   SRCOFFS_DHIRES +
                       10 * (static_cast<uint8_t>(
                                (static_cast<uint16_t>(VALUE) >> 8) & 0xFF)) +
                       COLOR,
                   (static_cast<uint8_t>(VALUE)) << 1);
#undef COLOR
#undef VALUE
      }
      {
        constexpr int PIXEL = 7;
#define COLOR ((xpixel + PIXEL) & 3)
#define VALUE (dwordval >> (4 + PIXEL - COLOR))
        CopySource(xpixel + PIXEL, ypixel + (yoffset >> 9), 7, 2,
                   SRCOFFS_DHIRES +
                       10 * (static_cast<uint8_t>(
                                (static_cast<uint16_t>(VALUE) >> 8) & 0xFF)) +
                       COLOR,
                   (static_cast<uint8_t>(VALUE)) << 1);
#undef COLOR
#undef VALUE
      }
      dirty = true;
    }
    yoffset += 0x400;
  }

  return dirty;
}

auto MixColors(uint8_t c1, uint8_t c2) -> uint8_t {
#define COMBINATION(c1, c2, ref1, ref2) \
  (((c1) == (ref1) && (c2) == (ref2)) || ((c1) == (ref2) && (c2) == (ref1)))

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
    return MONOCHROME_CUSTOM;  // visible failure indicator
  }

#undef COMBINATION
}

auto video_create_color_mix_map() -> void {
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

static inline auto clamp_mix(int idx) -> int {
  return (idx >= 0 && idx < 6) ? idx : 0;
}

void MixColorsVertical(int matx, int maty) {
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

  twoHalfPixel = colormixmap[clamp_mix(hgrpixelmatrix[matx][maty - 2] & 0x0F)]
                            [clamp_mix(hgrpixelmatrix[matx][maty - 1] & 0x0F)]
                            [clamp_mix(hgrpixelmatrix[matx][maty] & 0x0F)];
  colormixbuffer[0] = (twoHalfPixel & 0xFF00) >> 8;
  colormixbuffer[1] = twoHalfPixel & 0x00FF;

  twoHalfPixel =
      colormixmap[clamp_mix(hgrpixelmatrix[matx][maty - 1] & 0x0F)][clamp_mix(
          hgrpixelmatrix[matx][maty] & 0x0F)][clamp_mix(bot1idx)];
  colormixbuffer[2] = (twoHalfPixel & 0xFF00) >> 8;
  colormixbuffer[3] = twoHalfPixel & 0x00FF;

  twoHalfPixel = colormixmap[clamp_mix(hgrpixelmatrix[matx][maty] & 0x0F)]
                            [clamp_mix(bot1idx)][clamp_mix(bot2idx)];
  colormixbuffer[4] = (twoHalfPixel & 0xFF00) >> 8;
  colormixbuffer[5] = twoHalfPixel & 0x00FF;
}

void CopyMixedSource(int x, int y, int sourcex, int sourcey) {
  uint8_t* currsourceptr = g_source_start_of_line[sourcey] + sourcex;
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
  if (!vidlastmem) return false;
  (void)y;
  bool dirty = false;
  int yoffset = 0;
  while (yoffset < 0x2000) {
    uint8_t byteval1 = (x > 0) ? *(g_hires_bank0 + offset + yoffset - 1) : 0;
    uint8_t byteval2 = *(g_hires_bank0 + offset + yoffset);
    uint8_t byteval3 = (x < 39) ? *(g_hires_bank0 + offset + yoffset + 1) : 0;
    if ((byteval2 != *(vidlastmem.get() + offset + yoffset + 0x2000)) ||
        ((x > 0) &&
         ((byteval1 & 0x60) !=
          (*(vidlastmem.get() + offset + yoffset + 0x1FFF) & 0x60))) ||
        ((x < 39) &&
         ((byteval3 & 0x03) !=
          (*(vidlastmem.get() + offset + yoffset + 0x2001) & 0x03))) ||
        redrawfull || video_worker_active_) {
#define COLOFFS (((byteval1 & 0x60) << 2) | ((byteval3 & 0x03) << 5))
      if (g_videotype == VT_COLOR_TVEMU) {
        CopyMixedSource(xpixel >> 1, (ypixel + (yoffset >> 9)) >> 1,
                        SRCOFFS_HIRES + COLOFFS + ((x & 1) << 4),
                        ((static_cast<int>(byteval2)) << 1));
      } else {
        CopySource(xpixel, ypixel + (yoffset >> 9), 14, 2,
                   SRCOFFS_HIRES + COLOFFS + ((x & 1) << 4),
                   ((static_cast<int>(byteval2)) << 1));
      }
#undef COLOFFS
      dirty = true;
    }
    yoffset += 0x400;
  }

  return dirty;
}

auto UpdateLoResCell(int x, int y, int xpixel, int ypixel, int offset) -> bool {
  if (!vidlastmem) return false;
  (void)y;
  uint8_t val = *(g_text_bank0 + offset);
  if ((val != *(vidlastmem.get() + offset + 0x400)) || redrawfull ||
      video_worker_active_) {
    CopySource(xpixel, ypixel, 14, 8, SRCOFFS_LORES + ((x & 1) << 1),
               ((val & 0xF) << 4));
    CopySource(xpixel, ypixel + 8, 14, 8, SRCOFFS_LORES + ((x & 1) << 1),
               (val & 0xF0));
    return true;
  }
  return false;
}

auto UpdateDLoResCell(int x, int y, int xpixel, int ypixel, int offset)
    -> bool {
  if (!vidlastmem) return false;
  (void)y;
  uint8_t auxval = *(g_text_bank1 + offset);
  uint8_t mainval = *(g_text_bank0 + offset);

  if ((auxval != *(vidlastmem.get() + offset)) ||
      (mainval != *(vidlastmem.get() + offset + 0x400)) || redrawfull ||
      video_worker_active_) {
    CopySource(xpixel, ypixel, 7, 8, SRCOFFS_LORES + ((x & 1) << 1),
               ((auxval & 0xF) << 4));
    CopySource(xpixel, ypixel + 8, 7, 8, SRCOFFS_LORES + ((x & 1) << 1),
               (auxval & 0xF0));
    CopySource(xpixel + 7, ypixel, 7, 8, SRCOFFS_LORES + ((x & 1) << 1),
               ((mainval & 0xF) << 4));
    CopySource(xpixel + 7, ypixel + 8, 7, 8, SRCOFFS_LORES + ((x & 1) << 1),
               (mainval & 0xF0));
    return true;
  }
  return false;
}

auto LoadCharset() -> VideoSurface_t* {
  VideoSurface_t* result = nullptr;

  if ((g_apple2_type == A2TYPE_APPLE2) ||
      (g_apple2_type == A2TYPE_APPLE2PLUS)) {
    // character bitmap for II and IIplus
    result = video_load_xpm(charset40_IIplus_xpm);
  } else {
    switch (g_language) {
      case A2LANG_UK:
        result = video_load_xpm(charset40_british_xpm);
        break;
      case A2LANG_FR:
        result = video_load_xpm(charset40_french_xpm);
        break;
      case A2LANG_DE:
        result = video_load_xpm(charset40_german_xpm);
        break;
      case A2LANG_US:  // fall-through
      default:
        // character bitmap for IIe and enhanced
        result = video_load_xpm(charset40_xpm);
    }
  }

  if (result) {
    /* correct character set bitmaps should be 128x128 (single language) or
     * 256x128 for the Euro-ROMs with alternative language */
    if (((result->h != 128) && (result->h != 256)) || (result->w != 128)) {
      printf("ERROR: loaded character set has an unexpected size: %ix%i\n",
             result->w, result->h);
    }

    // enable second language support when charset has the double height (256
    // instead of 128 pixels)
    multi_language_charset = (result->h == 256);
    printf("Charset supports a second language: %s\n",
           (multi_language_charset) ? "YES" : "NO");
  }
  return result;
}

// All globally accessible functions are below this line

auto video_apparently_dirty() -> bool {
  if (SW_MIXED || redrawfull || video_worker_active_) {
    return true;
  }
  uint32_t address =
      (SW_HIRES && !SW_TEXT) ? (0x20 << displaypage2) : (0x4 << displaypage2);
  uint32_t length = (SW_HIRES && !SW_TEXT) ? 0x20 : 0x4;
  while (length--) {
    if (*(memdirty + (address++)) & 2) {
      return true;
    }
  }

  bool char_flashing = false;

  // Scan visible text page for any flashing chars
  if ((SW_TEXT || SW_MIXED) && (g_alt_char_set_offset == 0)) {
    uint8_t* pnMemText = mem_get_main_ptr(0x400 << displaypage2);

    // Scan 8 long-lines of 120 chars (at 128 char offsets):
    // . Skip 8-char holes in TEXT
    for (uint32_t y = 0; y < 8; y++) {
      for (uint32_t x = 0; x < 40 * 3; x++) {
        uint8_t ch = pnMemText[y * 128 + x];
        if ((ch >= 0x40) && (ch <= 0x7F)) {
          char_flashing = true;
          break;
        }
      }
    }
  }

  if (char_flashing) {
    return true;
  }
  return false;
}

auto video_benchmark() -> void {
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  int loop = 0;
  auto* mem32 = reinterpret_cast<uint32_t*>(mem);
  for (loop = 4096; loop < 6144; loop++) {
    *(mem32 + loop) =
        ((loop & 1) ^ ((loop & 0x40) >> 6)) ? 0x14141414 : 0xAAAAAAAA;
  }
  for (loop = 6144; loop < 8192; loop++) {
    *(mem32 + loop) =
        ((loop & 1) ^ ((loop & 0x40) >> 6)) ? 0xAAAAAAAA : 0x14141414;
  }

  uint32_t totaltextfps = 0;
  g_video_mode = VF_TEXT;
  memset(mem + 0x400, 0x14, 0x400);
  video_redraw_screen();
  auto milliseconds = static_cast<uint32_t>(GetTickCount());
  while (GetTickCount() == milliseconds);
  milliseconds = static_cast<uint32_t>(GetTickCount());
  uint32_t cycle = 0;
  do {
    if (cycle & 1) {
      memset(mem + 0x400, 0x14, 0x400);
    } else {
      memcpy(mem + 0x400, mem + ((cycle & 2) ? 0x4000 : 0x6000), 0x400);
    }
    video_refresh_screen();
    if (cycle++ >= 3) {
      cycle = 0;
    }
    totaltextfps++;
  } while (GetTickCount() - milliseconds < 1000);

  uint32_t totalhiresfps = 0;
  g_video_mode = VF_HIRES;
  memset(mem + 0x2000, 0x14, 0x2000);
  video_redraw_screen();
  milliseconds = static_cast<uint32_t>(GetTickCount());
  while (GetTickCount() == milliseconds);
  milliseconds = static_cast<uint32_t>(GetTickCount());
  cycle = 0;
  do {
    if (cycle & 1) {
      memset(mem + 0x2000, 0x14, 0x2000);
    } else {
      memcpy(mem + 0x2000, mem + ((cycle & 2) ? 0x4000 : 0x6000), 0x2000);
    }
    video_refresh_screen();
    if (cycle++ >= 3) {
      cycle = 0;
    }
    totalhiresfps++;
  } while (GetTickCount() - milliseconds < 1000);

  cpu_setup_benchmark();
  uint32_t totalmhz10 = 0;
  milliseconds = static_cast<uint32_t>(GetTickCount());
  while (GetTickCount() == milliseconds);
  milliseconds = static_cast<uint32_t>(GetTickCount());
  cycle = 0;
  do {
    cpu_execute(100000);
    totalmhz10++;
  } while (GetTickCount() - milliseconds < 1000);

  if ((cpu_get_registers()->pc < 0x300) || (cpu_get_registers()->pc > 0x400)) {
    printf(
        "The emulator has detected a problem while running the CPU "
        "benchmark.\n");

    bool error = false;
    uint16_t lastpc = 0x300;
    int loop = 0;
    while ((loop < 10000) && !error) {
      cpu_setup_benchmark();
      cpu_execute(loop);
      if ((cpu_get_registers()->pc < 0x300) ||
          (cpu_get_registers()->pc > 0x400)) {
        error = true;
      } else {
        lastpc = cpu_get_registers()->pc;
        ++loop;
      }
    }
    if (error) {
      printf(
          "The emulator experienced an error %u clock cycles into the CPU "
          "benchmark.\n",
          static_cast<unsigned>(loop));
      printf("Prior to the error, the program counter was at $%04X.\n",
             static_cast<unsigned>(lastpc));
      printf(" After the error, it had jumped to $%04X.\n",
             static_cast<unsigned>(cpu_get_registers()->pc));
    } else {
      printf(
          "The emulator was unable to locate the exact point of the error.\n");
    }
  }

  uint32_t realisticfps = 0;
  memset(mem + 0x2000, 0xAA, 0x2000);
  video_redraw_screen();
  milliseconds = static_cast<uint32_t>(GetTickCount());
  while (GetTickCount() == milliseconds);
  milliseconds = static_cast<uint32_t>(GetTickCount());
  cycle = 0;
  do {
    if (realisticfps < 10) {
      int cycles = 100000;
      while (cycles > 0) {
        uint32_t executedcycles = cpu_execute(static_cast<uint32_t>(103));
        cycles -= executedcycles;
        video_update_vbl(0);
      }
    }
    if (cycle & 1) {
      memset(mem + 0x2000, 0xAA, 0x2000);
    } else {
      memcpy(mem + 0x2000, mem + ((cycle & 2) ? 0x4000 : 0x6000), 0x2000);
    }
    video_refresh_screen();
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

auto video_check_mode(uint16_t, uint16_t address, uint8_t, uint8_t,
                      uint32_t cycles_left) -> uint8_t {
  address &= 0xFF;
  if (address == 0x7F) {
    return mem_read_floating_bus(SW_DHIRES != 0, cycles_left);
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
        result = g_alt_char_set_offset != 0;
        break;
      case 0x1F:
        result = SW_80COL;
        break;
      case 0x7F:
        result = SW_DHIRES;
        break;
      default:
        break;
    }
    return (mem_read_floating_bus(cycles_left) & 0x7F) | (result ? 0x80 : 0);
  }
}

auto video_check_page(bool force) -> void {
  if ((displaypage2 != (SW_PAGE2 != 0)) &&
      (force || (emul_msec - lastpageflip > 500))) {
    displaypage2 = (SW_PAGE2 != 0);
    video_refresh_screen();
    lastpageflip = emul_msec;
  }
}

auto video_check_vbl(uint16_t, uint16_t, uint8_t, uint8_t, uint32_t cycles_left)
    -> uint8_t {
  bool vbl_bar = false;
  video_get_scanner_address(&vbl_bar, cycles_left);
  uint8_t r = mem_read_floating_bus(cycles_left);
  return static_cast<uint8_t>((r & ~0x80) | ((vbl_bar) ? 0x80 : 0));
}

auto video_choose_color() -> void {}

auto video_destroy() -> void {
  {
    video_worker_terminate_ = true;
    video_cv.notify_all();
    if (video_worker_active_) {
      if (video_worker_thread_.joinable()) video_worker_thread_.join();
    }
    video_worker_active_ = false;
  }

  vidlastmem.reset();
  if (g_device_bitmap) {
    video_destroy_surface(g_device_bitmap);
  }
  g_device_bitmap = nullptr;

  if (g_origscreen) {
    video_destroy_surface(g_origscreen);
  }
  g_origscreen = nullptr;

  if (g_status_surface) {
    video_destroy_surface(g_status_surface);
  }
  g_status_surface = nullptr;

  if (g_source_bitmap) {
    video_destroy_surface(g_source_bitmap);
  }
  g_source_bitmap = nullptr;

  if (g_logo_bitmap && (g_logo_bitmap != assets->splash)) {
    video_destroy_surface(g_logo_bitmap);
  }
  g_logo_bitmap = nullptr;

  if (charset40) {
    video_destroy_surface(charset40);
  }
  charset40 = nullptr;

  if (font_sfc && (font_sfc != assets->font)) {
    video_destroy_surface(font_sfc);
  }
  font_sfc = nullptr;
}

auto video_display_logo() -> void {
  VideoRect_t drect{}, srect{};

  if (!g_logo_bitmap) {
    return;
  }

  // Clear logo destination if needed, but normally we just stretch to it
  srect.x = srect.y = 0;
  srect.w = g_logo_bitmap->w;
  srect.h = g_logo_bitmap->h;

  drect.x = drect.y = 0;
  drect.w = 560;  // Standard output width
  drect.h = 384;  // Standard output height

  if (g_device_bitmap) {
    video_soft_stretch(g_logo_bitmap, &srect, g_device_bitmap, &drect);
  }
}

auto video_has_refreshed() -> bool {
  bool result = hasrefreshed;
  hasrefreshed = false;
  return result;
}

auto video_initialize() -> void {
  static bool mutex_initialized = false;
  if (!mutex_initialized) {
    mutex_initialized = true;
  }

  vidlastmem.reset(static_cast<uint8_t*>(malloc(0x10000)));
  if (vidlastmem) {
    memset(vidlastmem.get(), 0, 0x10000);
  }

  if (assets != nullptr) {
    g_logo_bitmap = assets->splash;
    if (font_sfc == nullptr) {
      font_sfc = assets->font;
    }
  }

  CreateIdentityPalette();

  for (int index = DARK_RED; index < NUM_COLOR_PALETTE; index++) {
    customcolors[index - DARK_RED] =
        RGB(framebufferinfo[index].r, framebufferinfo[index].g,
            framebufferinfo[index].b);
  }

  CreateDIBSections();
  video_reset_state();

  if (!g_singlethreaded) {
    VideoInitWorker();
  }
}

auto video_next_scheduled_update_ = std::chrono::system_clock::now();
auto video_set_next_scheduled_update() -> void {
  if (!g_singlethreaded) {
    video_next_scheduled_update_ = std::chrono::system_clock::now();
    std::this_thread::yield();
  }
}

void VideoWorkerThread() {
  while (!video_worker_terminate_) {
    std::unique_lock<std::mutex> lck(s_video_worker_mutex);
    video_cv.wait_until(lck, video_next_scheduled_update_, [] {
      return video_worker_refresh_.load() || video_worker_terminate_.load();
    });
    if (video_worker_terminate_) break;
    if (video_worker_refresh_) {
      video_perform_refresh();
      video_worker_refresh_ = false;
      std::this_thread::yield();
    }
  }
}

auto VideoInitWorker() -> bool {
  if (video_worker_active_ && video_worker_thread_.joinable()) {
    return true;
  }
  video_worker_terminate_ = false;
  video_worker_active_ = true;
  try {
    video_worker_thread_ = std::thread(VideoWorkerThread);
  } catch (...) {
    // If failed to start, revert to singlethreaded
    std::cerr << "FAILED to start video worker; reverting to single-threaded "
                 "video updating..."
              << std::endl;
    g_singlethreaded = true;
    video_worker_active_ = false;
  }
  return true;
}

auto video_realize_palette() -> void {}

auto video_redraw_screen() -> void {
  redrawfull = true;
  video_refresh_screen();
}

void VideoUpdateOutputBuffer() {
  VideoRect_t s = {0, 0, 560, 384};
  VideoSurface_t dst{};
  dst.pixels = reinterpret_cast<uint8_t*>(g_video_output);
  dst.w = 560;
  dst.h = 384;
  dst.pitch = 560 * 4;
  dst.bpp = 4;

  if (!g_device_bitmap) return;

  // Convert internal INDEX8 bitmap to RGB32 output buffer
  video_soft_stretch(g_device_bitmap, &s, &dst, &s);

  // If status panel is visible, overlay it
  if (g_status_cycle > 0 && g_show_leds && g_status_surface) {
    VideoRect_t ss = {0, 0, STATUS_PANEL_W, STATUS_PANEL_H};
    VideoRect_t ds = {560 - STATUS_PANEL_W - 5, 384 - STATUS_PANEL_H - 5,
                      STATUS_PANEL_W, STATUS_PANEL_H};
    video_soft_stretch(g_status_surface, &ss, &dst, &ds);
  }
}

auto video_perform_refresh() -> void {
  g_video_draw_mutex.lock();

  uint8_t rocker = 0;
  size_t rocker_sz = sizeof(rocker);
  if (peripheral_query(0, keyboard_query_rocker, &rocker, &rocker_sz) ==
      peripheral_ok) {
    s_language_rocker_switch = (rocker != 0);
  }

  displaypage2_latched = displaypage2;
  vidmode_latched = g_video_mode;

  if (g_state.mode == MODE_DEBUG) {
    if (redrawfull == 0) {
      g_video_draw_mutex.unlock();
      return;
    }
    if (g_debug_video_mode > 0) {
      vidmode_latched = g_debug_video_mode;
      displaypage2_latched = (g_debug_video_mode & VF_PAGE2) > 0;
      g_debug_video_mode = 0;
    }
  }

  uint8_t* addr = framebufferbits;
  int pitch = 560;
  CreateFrameOffsetTable(addr, pitch);

  if (g_singlethreaded) {
    g_hires_bank1 = mem_get_aux_ptr(0x2000 << displaypage2_latched);
    g_hires_bank0 = mem_get_main_ptr(0x2000 << displaypage2_latched);
    g_text_bank1 = mem_get_aux_ptr(0x0400 << displaypage2_latched);
    g_text_bank0 = mem_get_main_ptr(0x0400 << displaypage2_latched);
  } else {
    // One-level pipelining to allow CPU emulation to run concurrently without
    // display glitches.
    memcpy(display_pipeline_, mem_get_aux_ptr(0x2000 << displaypage2_latched),
           0x2000);
    memcpy(display_pipeline_ + 0x2000,
           mem_get_main_ptr(0x2000 << displaypage2_latched), 0x2000);
    memcpy(display_pipeline_ + 0x4000,
           mem_get_aux_ptr(0x0400 << displaypage2_latched), 0x0400);
    memcpy(display_pipeline_ + 0x4400,
           mem_get_main_ptr(0x0400 << displaypage2_latched), 0x0400);

    g_hires_bank1 = reinterpret_cast<uint8_t*>(display_pipeline_);
    g_hires_bank0 = reinterpret_cast<uint8_t*>(display_pipeline_) + 0x2000;
    g_text_bank1 = reinterpret_cast<uint8_t*>(display_pipeline_) + 0x4000;
    g_text_bank0 = reinterpret_cast<uint8_t*>(display_pipeline_) + 0x4400;
  }
  memset(celldirty, 0, static_cast<size_t>(40 * 32));
  UpdateFunc_t update =
      SWL_TEXT ? SWL_80COL ? Update80ColCell : Update40ColCell
      : SWL_HIRES
          ? (SWL_DHIRES && SWL_80COL) ? UpdateDHiResCell : UpdateHiResCell
      : (SWL_DHIRES && SWL_80COL) ? UpdateDLoResCell
                                  : UpdateLoResCell;

  bool anydirty = redrawfull | g_text_flash_flag;

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
    g_text_flash_flag = false;
  }

  if (g_status_cycle > 0) {
    g_status_cycle--;
    if (!g_status_cycle) {
      peripheral_command(7, harddisk_cmd_reset_status, nullptr, 0);
    }
  }

  // Update final output buffer
  VideoUpdateOutputBuffer();

  g_frame_ready = true;

  SetLastDrawnImage();
  redrawfull = false;
  hasrefreshed = true;

  g_video_draw_mutex.unlock();
}

auto video_reinitialize() -> void {
  CreateIdentityPalette();
  CreateDIBSections();
}

auto video_refresh_screen(uint32_t redraw_whole_screen_video_mode /* =0*/,
                          bool redraw_whole_screen /* =false*/) -> void {
  // If multithreaded, tell thread to do it; otherwise, do it in this thread
  if (redraw_whole_screen) {
    g_debug_video_mode = redraw_whole_screen_video_mode;
    redrawfull = true;
  }
  if (video_worker_active_) {
    {
      std::lock_guard<std::mutex> lock(s_video_worker_mutex);
      video_worker_refresh_ = true;
    }
    video_cv.notify_one();
  } else {
    // If singlethreaded just call the refresh here.
    video_perform_refresh();
    hasrefreshed = true;
  }
}

auto video_reset_state() -> void {
  g_alt_char_set_offset = 0;
  displaypage2 = false;
  g_video_mode = VF_TEXT;
  redrawfull = true;
}

auto video_set_mode(uint16_t, uint16_t address, uint8_t write, uint8_t,
                    uint32_t cycles_left) -> uint8_t {
  (void)write;

  address &= 0xFF;
  int oldvalue = g_alt_char_set_offset +
                 static_cast<int>(g_video_mode & ~(VF_MASK2 | VF_PAGE2));
  switch (address) {
    case 0x00:
      g_video_mode &= ~VF_MASK2;
      break;
    case 0x01:
      g_video_mode |= VF_MASK2;
      break;
    case 0x0C:
      if (!IS_APPLE2()) {
        g_video_mode &= ~VF_80COL;
      }
      break;
    case 0x0D:
      if (!IS_APPLE2()) {
        g_video_mode |= VF_80COL;
      }
      break;
    case 0x0E:
      if (!IS_APPLE2()) {
        g_alt_char_set_offset = 0;
      }
      break;
    case 0x0F:
      if (!IS_APPLE2()) {
        g_alt_char_set_offset = 256;
      }
      break;
    case 0x50:
      g_video_mode &= ~VF_TEXT;
      break;
    case 0x51:
      g_video_mode |= VF_TEXT;
      break;
    case 0x52:
      g_video_mode &= ~VF_MIXED;
      break;
    case 0x53:
      g_video_mode |= VF_MIXED;
      break;
    case 0x54:
      g_video_mode &= ~VF_PAGE2;
      break;
    case 0x55:
      g_video_mode |= VF_PAGE2;
      break;
    case 0x56:
      g_video_mode &= ~VF_HIRES;
      break;
    case 0x57:
      g_video_mode |= VF_HIRES;
      break;
    case 0x5E:
      if (!IS_APPLE2()) {
        g_video_mode |= VF_DHIRES;
      }
      break;
    case 0x5F:
      if (!IS_APPLE2()) {
        g_video_mode &= ~VF_DHIRES;
      }
      break;
    default:
      break;
  }
  if (SW_MASK2) {
    g_video_mode &= ~VF_PAGE2;
  }
  if (oldvalue != g_alt_char_set_offset +
                      static_cast<int>(g_video_mode & ~(VF_MASK2 | VF_PAGE2))) {
    graphicsmode = !SW_TEXT;
    redrawfull = true;
    video_refresh_screen();
  }

  if (displaypage2 != (SW_PAGE2 != 0)) {
    displaypage2 = (SW_PAGE2 != 0);
    redrawfull = true;
    video_refresh_screen();
  }

  return mem_read_floating_bus(cycles_left);
}

static uint32_t g_video_cycles_in_frame = 0;
auto video_update_vbl(uint32_t cycles_this_frame) -> void {
  g_video_cycles_in_frame += cycles_this_frame;
  while (g_video_cycles_in_frame >= g_state.clks_per_frame) {
    g_video_cycles_in_frame -= g_state.clks_per_frame;
    video_refresh_screen();
    video_update_flash();
  }
}

// Called at 60Hz (every 16.666ms)
auto video_update_flash() -> void {
  static uint32_t text_flash_cnt = 0;
  text_flash_cnt++;
  if (text_flash_cnt == 60 / 6) {  // Flash rate = 6Hz (every 166ms)
    text_flash_cnt = 0;
    g_text_flash_state = !g_text_flash_state;

    if ((SW_TEXT || SW_MIXED)) {
      g_text_flash_flag = true;
    }
  }
}

auto video_get_sw_80col() -> bool { return SW_80COL != 0; }

auto video_get_sw_dhires() -> bool { return SW_DHIRES != 0; }

auto video_get_sw_hires() -> bool { return SW_HIRES != 0; }

auto video_get_sw_80store() -> bool { return SW_MASK2 != 0; }

auto video_get_sw_mixed() -> bool { return SW_MIXED != 0; }

auto video_get_sw_page2() -> bool { return SW_PAGE2 != 0; }

auto video_get_sw_text() -> bool { return SW_TEXT != 0; }

auto video_get_sw_alt_charset() -> bool { return g_alt_char_set_offset != 0; }

//===========================================================================
auto video_get_snapshot(SS_IO_Video* ss) -> uint32_t {
  if (!ss) return 1;
  ss->alt_char_set = g_alt_char_set_offset != 0;
  ss->vid_mode = g_video_mode;
  return 0;
}

auto video_set_snapshot(SS_IO_Video* ss) -> uint32_t {
  if (!ss) return 1;
  g_alt_char_set_offset = !ss->alt_char_set ? 0 : 256;
  g_video_mode = ss->vid_mode;

  graphicsmode = !SW_TEXT;
  displaypage2 = (SW_PAGE2 != 0);

  return 0;
}

auto video_get_scanner_address(bool* pbVblBar_OUT,
                               const uint32_t executed_cycles) -> uint16_t {
  if (g_state.clks_per_frame == 0) return 0;
  // get video scanner position
  int cycles =
      (g_video_cycles_in_frame + executed_cycles) % g_state.clks_per_frame;

  // machine state switches
  int hires = (SW_HIRES & !SW_TEXT) ? 1 : 0;
  int page2 = (SW_PAGE2) ? 1 : 0;
  int n80Store = (mem_get_80store()) ? 1 : 0;

  // calculate video parameters according to display standard
  int scan_lines = g_state.video_scanner_ntsc ? kNTSCScanLines : kPALScanLines;

  // calculate horizontal scanning state
  int h_clock =
      (cycles + kHPEClock) % kHClocks;    // which horizontal scanning clock
  int h_state = kHClock0State + h_clock;  // H state bits
  if (h_clock >= kHPresetClock) {         // check for horizontal preset
    h_state -= 1;  // correct for state preset (two 0 states)
  }
  int h_0 = (h_state >> 0) & 1;
  int h_1 = (h_state >> 1) & 1;
  int h_2 = (h_state >> 2) & 1;
  int h_3 = (h_state >> 3) & 1;
  int h_4 = (h_state >> 4) & 1;
  int h_5 = (h_state >> 5) & 1;

  // calculate vertical scanning state
  int v_line = cycles / kHClocks;       // which vertical scanning line
  int v_state = kVLine0State + v_line;  // V state bits
  if ((v_line >= kVPresetLine)) {  // check for previous vertical state preset
    v_state -= scan_lines;         // compensate for preset
  }
  int v_A = (v_state >> 0) & 1;
  int v_B = (v_state >> 1) & 1;
  int v_C = (v_state >> 2) & 1;
  int v_0 = (v_state >> 3) & 1;
  int v_1 = (v_state >> 4) & 1;
  int v_2 = (v_state >> 5) & 1;
  int v_3 = (v_state >> 6) & 1;
  int v_4 = (v_state >> 7) & 1;

  // calculate scanning memory address
  if (SW_HIRES && SW_MIXED && (v_4 & v_2)) {
    // The softswitch for this is $c053 for mixed, $c052 for fill (no text on
    // bottom).
    hires = 0;  // (address is in text memory)
  }

  int addend0 = 0x68;  // 1            1            0            1
  int addend1 = (h_5 << 5) | (h_4 << 4) | (h_3 << 3);
  int addend2 = (v_4 << 6) | (v_3 << 5) | (v_4 << 4) | (v_3 << 3);
  int sum = (addend0 + addend1 + addend2) & (0x0F << 3);

  int address = 0;
  address |= h_0 << 0;  // a0
  address |= h_1 << 1;  // a1
  address |= h_2 << 2;  // a2
  address |= sum;       // a3 - aa6
  address |= v_0 << 7;  // a7
  address |= v_1 << 8;  // a8
  address |= v_2 << 9;  // a9
  address |= ((hires) ? v_A : (1 ^ (page2 & (1 ^ n80Store)))) << 10;  // a10
  address |= ((hires) ? v_B : (page2 & (1 ^ n80Store))) << 11;        // a11
  if (hires) {                                                        // hires?
    // Y: insert hires only address bits
    address |= v_C << 12;                             // a12
    address |= (1 ^ (page2 & (1 ^ n80Store))) << 13;  // a13
    address |= (page2 & (1 ^ n80Store)) << 14;        // a14
  } else {
    // N: text, so no higher address bits unless Apple ][, not Apple //e
    if ((IS_APPLE2()) &&           // Apple ][?
        (kHPEClock <= h_clock) &&  // Y: HBL?
        (h_clock <= (kHClocks - 1))) {
      address |= 1 << 12;  // Y: a12 (add $1000 to address!)
    }
  }

  if (pbVblBar_OUT != nullptr) {
    if (v_4 & v_3) {
      *pbVblBar_OUT = true;
    } else {
      *pbVblBar_OUT = false;
    }
  }
  return static_cast<uint16_t>(address);
}

auto video_get_vbl(const uint32_t executed_cycles) -> bool {
  if (g_state.clks_per_frame == 0) return false;
  // get cycles within current frame
  int cycles =
      (g_video_cycles_in_frame + executed_cycles) % g_state.clks_per_frame;

  // Apple II NTSC: 262 lines, 65 cycles per line.
  // Visible area: lines 0-191. VBL: lines 192-261.
  // VBL starts at cycle 192 * 65 = 12480.

  return (cycles >= 12480);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-constant-array-index,
// bugprone-easily-swappable-parameters, bugprone-branch-clone,
// bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions,
// bugprone-misplaced-widening-cast, bugprone-switch-missing-default-case,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-avoid-do-while, cppcoreguidelines-init-variables,
// cppcoreguidelines-macro-usage, cppcoreguidelines-no-malloc,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-type-reinterpret-cast,
// cppcoreguidelines-use-enum-class, google-readability-casting,
// modernize-avoid-c-style-cast)
