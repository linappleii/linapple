// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

struct SsIoVideo_t;
struct VideoSurface;
struct VideoColor;

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

constexpr int SCREEN_WIDTH = 560;
constexpr int SCREEN_HEIGHT = 384;

constexpr int VIEWPORTX = 5;
constexpr int VIEWPORTY = 5;
constexpr int VIEWPORTCX = 560;
constexpr int VIEWPORTCY = 384;

constexpr int STATUS_PANEL_W = 100;
constexpr int STATUS_PANEL_H = 48;

using ColorRef_t = uint32_t;

struct Point_t {
  int32_t x{0};
  int32_t y{0};
};

struct Rect_t {
  int32_t left{0};
  int32_t top{0};
  int32_t right{0};
  int32_t bottom{0};
};

enum VideoType_t : uint8_t {
  VT_MONO_CUSTOM,
  VT_COLOR_STANDARD,
  VT_COLOR_TEXT_OPTIMIZED,
  VT_COLOR_TVEMU,
  VT_COLOR_HALF_SHIFT_DIM,
  VT_MONO_AMBER,
  VT_MONO_GREEN,
  VT_MONO_WHITE,
  VT_NUM_MODES,
};
using VIDEOTYPE = VideoType_t;

enum VideoFlag_t : uint8_t {
  VF_80COL = 0x00000001,
  VF_DHIRES = 0x00000002,
  VF_HIRES = 0x00000004,
  VF_MASK2 = 0x00000008,
  VF_MIXED = 0x00000010,
  VF_PAGE2 = 0x00000020,
  VF_TEXT = 0x00000040,
};

enum AppleFont_t : uint16_t {
  APPLE_FONT_WIDTH = 14,
  APPLE_FONT_HEIGHT = 16,
  APPLE_FONT_CELL_WIDTH = 16,
  APPLE_FONT_CELL_HEIGHT = 16,
  APPLE_FONT_X_REGIONSIZE = 256,
  APPLE_FONT_Y_REGIONSIZE = 256,
  APPLE_FONT_Y_APPLE_2PLUS = 0,
  APPLE_FONT_Y_APPLE_80COL = 256,
  APPLE_FONT_Y_APPLE_40COL = 512,
};

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

constexpr uint32_t GREEN_SHIFT = 8;
constexpr uint32_t BLUE_SHIFT = 16;

constexpr auto RGB(uint8_t r, uint8_t g, uint8_t b) noexcept -> uint32_t {
  return (static_cast<uint32_t>(r)) |
         (static_cast<uint32_t>(g) << GREEN_SHIFT) |
         (static_cast<uint32_t>(b) << BLUE_SHIFT);
}

enum ColorPaletteIndex_t : uint8_t {
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
  NUM_COLOR_PALETTE,
};
using Color_Palette_Index_e = ColorPaletteIndex_t;

extern int g_status_cycle;
extern bool g_show_leds;
extern bool graphicsmode;
extern uint32_t monochrome;
extern uint32_t g_videotype;
extern uint32_t g_video_mode;
extern bool g_singlethreaded;
extern std::recursive_mutex g_video_draw_mutex;
extern std::atomic<bool> g_frame_ready;

extern VideoSurface* g_logo_bitmap;
extern VideoSurface* g_status_surface;
extern VideoSurface* g_source_bitmap;
extern VideoSurface* g_device_bitmap;
extern VideoSurface* g_origscreen;

auto video_get_output_buffer() -> uint32_t*;
auto video_get_output_palette() -> VideoColor*;
inline auto video_is_frame_ready() noexcept -> bool {
  return g_frame_ready.load();
}
inline auto video_clear_frame_ready() noexcept -> void {
  g_frame_ready.store(false);
}
inline auto video_set_frame_ready(bool ready = true) noexcept -> void {
  g_frame_ready.store(ready);
}

auto video_create_color_mix_map() -> void;
auto video_apparently_dirty() noexcept -> bool;
auto video_benchmark() -> void;
auto video_check_page(bool force) -> void;
auto video_destroy() -> void;
auto video_display_logo() -> void;
auto video_has_refreshed() noexcept -> bool;
auto video_init_worker() -> bool;
auto video_initialize() -> void;
auto video_realize_palette() -> void;
auto video_set_next_scheduled_update() -> void;
auto video_set_rendering_enabled(bool enabled) -> void;
auto video_is_rendering_enabled() noexcept -> bool;
auto video_redraw_screen() -> void;
auto video_refresh_screen(uint32_t mode = 0, bool redraw_whole = false) -> void;
auto video_perform_refresh() -> void;
// The //e keyboard's rocker switch also selects the character generator's
// local half.
auto video_set_rocker_switch(bool local) -> void;
auto video_reinitialize() -> void;
auto video_reset_state() -> void;

auto video_get_scanner_address(bool* vbl_bar_out,
                               uint32_t executed_cycles) noexcept -> uint16_t;
auto video_get_vbl(uint32_t executed_cycles) noexcept -> bool;
auto video_update_vbl(uint32_t cycles_this_frame) -> void;
auto video_update_flash() -> void;

auto video_get_sw_80col() noexcept -> bool;
auto video_get_sw_dhires() noexcept -> bool;
auto video_get_sw_hires() noexcept -> bool;
auto video_get_sw_80store() noexcept -> bool;
auto video_get_sw_mixed() noexcept -> bool;
auto video_get_sw_page2() noexcept -> bool;
auto video_get_sw_text() noexcept -> bool;
auto video_get_sw_alt_charset() noexcept -> bool;

auto video_get_snapshot(SsIoVideo_t* ss) noexcept -> uint32_t;
auto video_set_snapshot(const SsIoVideo_t* ss) noexcept -> uint32_t;

auto video_check_mode(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                      uint32_t executed_cycles) -> uint8_t;
auto video_check_vbl(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                     uint32_t executed_cycles) -> uint8_t;
auto video_set_mode(uint16_t pc, uint16_t addr, uint8_t write, uint8_t d,
                    uint32_t executed_cycles) -> uint8_t;

auto set_budget_video(bool b) -> void;
auto get_budget_video() noexcept -> bool;
