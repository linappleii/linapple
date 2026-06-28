// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

struct SsIoVideo_t;
using SS_IO_Video = struct SsIoVideo_t;
#include "frontends/common/VideoSurface.h"

constexpr uint32_t apple2_visible_width = 280;
constexpr uint32_t apple2_visible_height = 192;
constexpr uint32_t video_scale_factor = 2;
constexpr uint32_t video_width = apple2_visible_width * video_scale_factor;
constexpr uint32_t video_height = apple2_visible_height * video_scale_factor;

constexpr uint32_t text_columns = 40;
constexpr uint32_t text_rows = 24;
constexpr uint32_t dirty_cell_rows = 32;
constexpr uint32_t max_palette_size = 256;
constexpr uint8_t default_gray_component = 0xC0;
constexpr uint32_t hgr_matrix_yoffset = 2;

#define SCREEN_WIDTH 560
#define SCREEN_HEIGHT 384

#define VIEWPORTX 5
#define VIEWPORTY 5
#define VIEWPORTCX 560
#define VIEWPORTCY 384

constexpr int STATUS_PANEL_W = 100;
constexpr int STATUS_PANEL_H = 48;

using ColorRef_t = uint32_t;

using Point_t = struct Point_tag {
  int32_t x;
  int32_t y;
};

using Rect_t = struct Rect_tag {
  int32_t left;
  int32_t top;
  int32_t right;
  int32_t bottom;
};

// Legacy macros for compatibility
#define APPLE2_VISIBLE_WIDTH apple2_visible_width
#define APPLE2_VISIBLE_HEIGHT apple2_visible_height
#define VIDEO_SCALE_FACTOR video_scale_factor
#define VIDEO_WIDTH video_width
#define VIDEO_HEIGHT video_height
#define TEXT_COLUMNS text_columns
#define TEXT_ROWS text_rows
#define DIRTY_CELL_ROWS dirty_cell_rows
#define MAX_PALETTE_SIZE max_palette_size
#define DEFAULT_GRAY_COMPONENT default_gray_component
#define HGR_MATRIX_YOFFSET hgr_matrix_yoffset

enum VideoType_t {
  VT_MONO_CUSTOM,
  VT_COLOR_STANDARD,
  VT_COLOR_TEXT_OPTIMIZED,
  VT_COLOR_TVEMU,
  VT_COLOR_HALF_SHIFT_DIM,
  VT_MONO_AMBER,
  VT_MONO_GREEN,
  VT_MONO_WHITE,
  VT_NUM_MODES
};
using VIDEOTYPE = VideoType_t;

enum VideoFlag_t {
  VF_80COL = 0x00000001,
  VF_DHIRES = 0x00000002,
  VF_HIRES = 0x00000004,
  VF_MASK2 = 0x00000008,
  VF_MIXED = 0x00000010,
  VF_PAGE2 = 0x00000020,
  VF_TEXT = 0x00000040
};
using VideoFlag_e = VideoFlag_t;

enum AppleFont_t {
  APPLE_FONT_WIDTH = 14,
  APPLE_FONT_HEIGHT = 16,
  APPLE_FONT_CELL_WIDTH = 16,
  APPLE_FONT_CELL_HEIGHT = 16,
  APPLE_FONT_X_REGIONSIZE = 256,
  APPLE_FONT_Y_REGIONSIZE = 256,
  APPLE_FONT_Y_APPLE_2PLUS = 0,
  APPLE_FONT_Y_APPLE_80COL = 256,
  APPLE_FONT_Y_APPLE_40COL = 512
};
using AppleFont_e = AppleFont_t;

constexpr uint8_t CREAM = 0xF6;
constexpr uint8_t MEDIUM_GRAY = 0xF7;
constexpr uint8_t DARK_GRAY = 0xF8;
constexpr uint8_t RED = 0xF9;
constexpr uint8_t GREEN = 0xFA;
constexpr uint8_t YELLOW = 0xFB;
constexpr uint8_t BLUE = 0xFC;
constexpr uint8_t MAGENTA = 0xFD;
constexpr uint8_t CYAN = 0xFE;
constexpr uint8_t WHITE = 0xFF;

inline auto RGB(uint8_t r, uint8_t g, uint8_t b) -> uint32_t {
  return (static_cast<uint32_t>(r)) | (static_cast<uint32_t>(g) << 8) |
         (static_cast<uint32_t>(b) << 16);
}

enum ColorPaletteIndex_t {
  BLACK,
  DARK_RED,
  DARK_GREEN,
  DARK_YELLOW,
  DARK_BLUE,
  DARK_MAGENTA,
  DARK_CYAN,
  LIGHT_GRAY,
  MONEY_GREEN,
  SKY_BLUE,
  DEEP_RED,
  LIGHT_BLUE,
  BROWN,
  ORANGE,
  PINK,
  AQUA,
  HGR_BLACK,
  HGR_WHITE,
  HGR_BLUE,
  HGR_RED,
  HGR_GREEN,
  HGR_MAGENTA,
  HGR_GREY1,
  HGR_GREY2,
  HGR_YELLOW,
  HGR_AQUA,
  HGR_PURPLE,
  HGR_PINK,
  MONOCHROME_CUSTOM,
  MONOCHROME_AMBER,
  MONOCHROME_GREEN,
  MONOCHROME_WHITE,
  DARKER_YELLOW,
  DARKEST_YELLOW,
  LIGHT_SKY_BLUE,
  DARKER_SKY_BLUE,
  DEEP_SKY_BLUE,
  DARKER_CYAN,
  DARKEST_CYAN,
  HALF_ORANGE,
  DARKER_BLUE,
  DARKER_GREEN,
  DARKEST_GREEN,
  LIGHTEST_GRAY,
  NUM_COLOR_PALETTE
};
using Color_Palette_Index_e = ColorPaletteIndex_t;

extern int g_iStatusCycle;
extern bool g_ShowLeds;
extern bool graphicsmode;
extern uint32_t monochrome;
extern uint32_t g_videotype;
extern uint32_t g_uVideoMode;
extern uint32_t g_singlethreaded;
extern std::recursive_mutex g_video_draw_mutex;
extern std::atomic<bool> g_bFrameReady;

extern VideoSurface_t* g_hLogoBitmap;
extern VideoSurface_t* g_hStatusSurface;
extern VideoSurface_t* g_hSourceBitmap;
extern VideoSurface_t* g_hDeviceBitmap;
extern VideoSurface_t* g_origscreen;

auto video_get_output_buffer() -> uint32_t*;
auto video_get_output_palette() -> VideoColor_t*;

auto video_set_budget(bool enable) -> void;
auto video_get_budget() -> bool;
auto video_set_current_clk6502() -> void;

auto video_create_color_mix_map() -> void;
auto video_apparently_dirty() -> bool;
auto video_benchmark() -> void;
auto video_check_page(bool page) -> void;
auto video_choose_color() -> void;
auto video_destroy() -> void;
auto video_draw_logo_bitmap() -> void;
auto video_display_logo() -> void;
auto video_has_refreshed() -> bool;
auto video_initialize() -> void;
auto video_realize_palette() -> void;
auto video_set_next_scheduled_update() -> void;
auto video_redraw_screen() -> void;
auto video_refresh_screen(uint32_t mode = 0, bool redraw_whole = false) -> void;
auto video_perform_refresh() -> void;
auto video_reinitialize() -> void;
auto video_reset_state() -> void;

auto video_get_scanner_address(bool* vbl_bar_out, uint32_t executed_cycles)
    -> uint16_t;
auto video_get_vbl(uint32_t executed_cycles) -> bool;
auto video_update_vbl(uint32_t cycles_this_frame) -> void;
auto video_update_flash() -> void;

auto video_get_sw_80col() -> bool;
auto video_get_sw_dhires() -> bool;
auto video_get_sw_hires() -> bool;
auto video_get_sw_80store() -> bool;
auto video_get_sw_mixed() -> bool;
auto video_get_sw_page2() -> bool;
auto video_get_sw_text() -> bool;
auto video_get_sw_alt_charset() -> bool;

auto video_get_snapshot(SS_IO_Video* ss) -> uint32_t;
auto video_set_snapshot(SS_IO_Video* ss) -> uint32_t;

auto video_check_mode(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                      uint32_t cycles_left) -> uint8_t;
auto video_check_vbl(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                     uint32_t cycles_left) -> uint8_t;
auto video_set_mode(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                    uint32_t cycles_left) -> uint8_t;

// Legacy C Declarations
auto VideoGetOutputBuffer() -> uint32_t*;
auto VideoGetOutputPalette() -> VideoColor_t*;
auto SetBudgetVideo(bool b) -> void;
auto GetBudgetVideo() -> bool;
auto SetCurrentCLK6502() -> void;
auto CreateColorMixMap() -> void;
auto VideoApparentlyDirty() -> bool;
auto VideoBenchmark() -> void;
auto VideoCheckPage(bool b) -> void;
auto VideoChooseColor() -> void;
auto VideoDestroy() -> void;
auto VideoDrawLogoBitmap() -> void;
auto VideoDisplayLogo() -> void;
auto VideoHasRefreshed() -> bool;
auto VideoInitialize() -> void;
auto VideoRealizePalette() -> void;
auto VideoSetNextScheduledUpdate() -> void;
auto VideoRedrawScreen() -> void;
auto VideoRefreshScreen(uint32_t m = 0, bool r = false) -> void;
auto VideoPerformRefresh() -> void;
auto VideoReinitialize() -> void;
auto VideoResetState() -> void;
auto VideoGetScannerAddress(bool* out, uint32_t cycles) -> uint16_t;
auto VideoGetVbl(uint32_t cycles) -> bool;
auto VideoUpdateVbl(uint32_t cycles) -> void;
auto VideoUpdateFlash() -> void;
auto VideoGetSW80COL() -> bool;
auto VideoGetSWDHIRES() -> bool;
auto VideoGetSWHIRES() -> bool;
auto VideoGetSW80STORE() -> bool;
auto VideoGetSWMIXED() -> bool;
auto VideoGetSWPAGE2() -> bool;
auto VideoGetSWTEXT() -> bool;
auto VideoGetSWAltCharSet() -> bool;
auto VideoGetSnapshot(SS_IO_Video* ss) -> uint32_t;
auto VideoSetSnapshot(SS_IO_Video* ss) -> uint32_t;
auto VideoCheckMode(uint16_t pc, uint16_t addr, uint8_t w, uint8_t d,
                    uint32_t c) -> uint8_t;
auto VideoCheckVbl(uint16_t pc, uint16_t addr, uint8_t w, uint8_t d, uint32_t c)
    -> uint8_t;
auto VideoSetMode(uint16_t pc, uint16_t addr, uint8_t w, uint8_t d, uint32_t c)
    -> uint8_t;
