#include "TuiVideo.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

struct TuiPixel_t {
  uint8_t r, g, b;
  bool operator==(const TuiPixel_t& other) const {
    return r == other.r && g == other.g && b == other.b;
  }
  bool operator!=(const TuiPixel_t& other) const { return !(*this == other); }
};

struct TuiState_t {
  uint8_t glyph[4];  // UTF-8 up to 4 bytes
  TuiPixel_t fg;
  TuiPixel_t bg;
  bool operator==(const TuiState_t& other) const {
    return memcmp(glyph, other.glyph, 4) == 0 && fg == other.fg &&
           bg == other.bg;
  }
  bool operator!=(const TuiState_t& other) const { return !(*this == other); }
};

static int g_term_width = 0;
static int g_term_height = 0;
static std::vector<TuiState_t> g_back_buffer;
static std::vector<TuiState_t> g_next_buffer;
static std::vector<char> g_output_buffer;
static uint32_t g_frame_count = 0;

static constexpr int default_term_width = 80;
static constexpr int default_term_height = 24;
static constexpr int utf8_glyph_size = 4;
static constexpr int output_reserve_factor = 64;
static constexpr int flash_divisor = 15;
static constexpr int a2_page1_addr = 0x400;
static constexpr int a2_page2_offset = 0x400;
static constexpr int a2_zero_page_offset = 0x00;
static constexpr int a2_cursor_x_addr = 0x24;
static constexpr int a2_cursor_y_addr = 0x25;
static constexpr int a2_cols_80 = 80;
static constexpr int a2_cols_40 = 40;
static constexpr int min_term_height_status = 24;
static constexpr int a2_text_rows = 24;
static constexpr int mixed_mode_text_start = 20;
static constexpr int refresh_full_divisor = 60;

auto tui_video_initialize() -> void {
  printf("\x1b[?7l\x1b[?25l");
  fflush(stdout);
  tui_video_on_resize();
}

auto tui_video_on_resize() -> void {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
    g_term_width = w.ws_col;
    g_term_height = w.ws_row;
  } else {
    g_term_width = default_term_width;
    g_term_height = default_term_height;
  }
  TuiState_t empty_cell{};
  memset(empty_cell.glyph, 0, utf8_glyph_size);
  empty_cell.glyph[0] = ' ';
  empty_cell.fg = {0, 0, 0};
  empty_cell.bg = {0, 0, 0};

  g_back_buffer.assign(static_cast<size_t>(g_term_width * g_term_height),
                       empty_cell);
  g_next_buffer.assign(static_cast<size_t>(g_term_width * g_term_height),
                       empty_cell);
  g_output_buffer.reserve(static_cast<size_t>(g_term_width * g_term_height *
                                              output_reserve_factor));
}

static auto get_text_addr(int row, int col) -> uint16_t {
  static const std::array<uint16_t, a2_text_rows> row_offsets = {
      0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
      0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0};
  return row_offsets.at(static_cast<size_t>(row)) + static_cast<uint16_t>(col);
}

static auto set_glyph(TuiState_t& state, const char* str) -> void {
  memset(state.glyph, 0, utf8_glyph_size);
  memcpy(state.glyph, str, utf8_glyph_size - 1);
}

static auto render_text_cell(int r, int c, bool is_80col, uint16_t page_offset,
                             bool alt_charset, bool flash_on, int hw_cursor_x,
                             int hw_cursor_y, TuiState_t& cell) -> void {
  uint8_t code = 0;
  if (is_80col) {
    if (c % 2 == 0) {
      code = *mem_get_aux_ptr(static_cast<uint16_t>(
          a2_page1_addr + page_offset + get_text_addr(r, c / 2)));
    } else {
      code = *mem_get_main_ptr(static_cast<uint16_t>(
          a2_page1_addr + page_offset + get_text_addr(r, c / 2)));
    }
  } else {
    code = *mem_get_main_ptr(static_cast<uint16_t>(a2_page1_addr + page_offset +
                                                   get_text_addr(r, c)));
  }

  uint8_t ascii = 0;
  uint8_t attr = 0;
  if (code >= 0x80) {
    attr = 1;
    ascii = code & 0x7F;
  } else if (code >= 0x40) {
    if (alt_charset) {
      attr = 1;
      ascii = code;
    } else {
      attr = 3;
      ascii = code - 0x40;
    }
  } else {
    attr = 2;
    ascii = code;
  }
  if (ascii < 0x20) {
    ascii += 0x40;
  }

  cell.glyph[0] = (ascii < 32 || ascii > 126) ? ' ' : static_cast<char>(ascii);
  cell.glyph[1] = 0;
  TuiPixel_t a2_white = {255, 255, 255};
  TuiPixel_t a2_black = {0, 0, 0};
  cell.fg = a2_white;
  cell.bg = a2_black;
  if (attr == 2 || (attr == 3 && !flash_on)) {
    cell.fg = a2_black;
    cell.bg = a2_white;
  }

  if (r == hw_cursor_y && c == hw_cursor_x) {
    if (flash_on) {
      set_glyph(cell, "\xe2\x96\x92");  // ▒ Checkerboard
      cell.fg = a2_white;
      cell.bg = a2_black;
    }
  }
}

auto tui_video_render_frame(const uint32_t* pixels, int width, int height,
                            int pitch) -> void {
  if (g_term_width <= 1 || g_term_height <= 1) {
    return;
  }
  g_frame_count++;
  bool flash_on = (g_frame_count / flash_divisor) % 2 == 0;

  bool is_text_mode = video_get_sw_text();
  bool is_mixed_mode = video_get_sw_mixed();
  bool is_80col = video_get_sw_80col();
  bool is_page2 = video_get_sw_page2();
  bool alt_charset = video_get_sw_alt_charset();
  uint16_t page_offset = is_page2 ? a2_page2_offset : 0x000;

  int hw_cursor_x = *mem_get_main_ptr(a2_cursor_x_addr);
  int hw_cursor_y = *mem_get_main_ptr(a2_cursor_y_addr);

  int a2_w_cols = is_80col ? a2_cols_80 : a2_cols_40;
  int avail_rows = g_term_height;
  bool show_status = (g_term_height > min_term_height_status);
  if (show_status) {
    avail_rows = g_term_height - 1;
  }

  TuiPixel_t a2_black = {0, 0, 0};
  TuiPixel_t bg_letterbox = {10, 10, 10};

  // Reset next buffer with letterbox color
  for (auto& cell : g_next_buffer) {
    cell.glyph[0] = ' ';
    cell.glyph[1] = 0;
    cell.fg = {40, 40, 40};
    cell.bg = bg_letterbox;
  }

  if (is_text_mode) {
    int display_h = a2_text_rows;
    int display_w = a2_w_cols;
    int off_x = (g_term_width - display_w) / 2;
    int off_y = (avail_rows - display_h) / 2;
    if (off_x < 0) off_x = 0;
    if (off_y < 0) off_y = 0;

    for (int r = 0; r < display_h; ++r) {
      int ty = off_y + r;
      if (ty >= avail_rows) break;
      for (int c = 0; c < display_w; ++c) {
        int tx = off_x + c;
        if (tx >= g_term_width) break;
        TuiState_t& cell =
            g_next_buffer.at(static_cast<size_t>(ty * g_term_width + tx));
        render_text_cell(r, c, is_80col, page_offset, alt_charset, flash_on,
                         hw_cursor_x, hw_cursor_y, cell);
      }
    }
  } else if (is_mixed_mode) {
    constexpr int mixed_text_lines = 4;
    int gfx_h =
        (avail_rows > mixed_text_lines) ? (avail_rows - mixed_text_lines) : 0;
    int gfx_w = gfx_h * 2 * 4 / 3;
    if (gfx_w > g_term_width) {
      gfx_w = g_term_width;
      gfx_h = gfx_w * 3 / 8;
    }

    int total_display_h = gfx_h + mixed_text_lines;
    int off_y = (avail_rows - total_display_h) / 2;
    if (off_y < 0) off_y = 0;

    int gfx_off_x = (g_term_width - gfx_w) / 2;
    if (gfx_off_x < 0) gfx_off_x = 0;

    int text_off_x = (g_term_width - a2_w_cols) / 2;
    if (text_off_x < 0) text_off_x = 0;

    int gfx_sample_height = height * 20 / 24;
    for (int y = 0; y < gfx_h; ++y) {
      int ty = off_y + y;
      if (ty >= avail_rows) break;
      for (int x = 0; x < gfx_w; ++x) {
        int tx = gfx_off_x + x;
        if (tx >= g_term_width) break;

        TuiState_t& cell =
            g_next_buffer.at(static_cast<size_t>(ty * g_term_width + tx));
        int sx = (gfx_w > 0) ? (x * width / gfx_w) : 0;
        int sy = (gfx_h > 0) ? (y * gfx_sample_height / gfx_h) : 0;
        set_glyph(cell, "\xe2\x96\x80");
        uint32_t p =
            pixels[static_cast<size_t>(sy) * (static_cast<size_t>(pitch) / 4) +
                   static_cast<size_t>(sx)];
        cell.fg = {static_cast<uint8_t>(p & 0xFF),
                   static_cast<uint8_t>((p >> 8) & 0xFF),
                   static_cast<uint8_t>((p >> 16) & 0xFF)};
        cell.bg = a2_black;
      }
    }

    for (int i = 0; i < mixed_text_lines; ++i) {
      int r = mixed_mode_text_start + i;
      int ty = off_y + gfx_h + i;
      if (ty >= avail_rows) break;
      for (int c = 0; c < a2_w_cols; ++c) {
        int tx = text_off_x + c;
        if (tx >= g_term_width) break;
        TuiState_t& cell =
            g_next_buffer.at(static_cast<size_t>(ty * g_term_width + tx));
        render_text_cell(r, c, is_80col, page_offset, alt_charset, flash_on,
                         hw_cursor_x, hw_cursor_y, cell);
      }
    }
  } else {
    int gfx_h = avail_rows;
    int gfx_w = gfx_h * 2 * 4 / 3;
    if (gfx_w > g_term_width) {
      gfx_w = g_term_width;
      gfx_h = gfx_w * 3 / 8;
    }
    int off_y = (avail_rows - gfx_h) / 2;
    if (off_y < 0) off_y = 0;
    int gfx_off_x = (g_term_width - gfx_w) / 2;
    if (gfx_off_x < 0) gfx_off_x = 0;

    for (int y = 0; y < gfx_h; ++y) {
      int ty = off_y + y;
      if (ty >= avail_rows) break;
      for (int x = 0; x < gfx_w; ++x) {
        int tx = gfx_off_x + x;
        if (tx >= g_term_width) break;

        TuiState_t& cell =
            g_next_buffer.at(static_cast<size_t>(ty * g_term_width + tx));
        int sx = (gfx_w > 0) ? (x * width / gfx_w) : 0;
        int sy = (gfx_h > 0) ? (y * height / gfx_h) : 0;
        set_glyph(cell, "\xe2\x96\x80");
        uint32_t p =
            pixels[static_cast<size_t>(sy) * (static_cast<size_t>(pitch) / 4) +
                   static_cast<size_t>(sx)];
        cell.fg = {static_cast<uint8_t>(p & 0xFF),
                   static_cast<uint8_t>((p >> 8) & 0xFF),
                   static_cast<uint8_t>((p >> 16) & 0xFF)};
        cell.bg = a2_black;
      }
    }
  }

  g_output_buffer.clear();
  g_output_buffer.push_back('\x1b');
  g_output_buffer.push_back('[');
  g_output_buffer.push_back('H');
  TuiPixel_t curr_fg = {1, 1, 1}, curr_bg = {1, 1, 1};

  for (int y = 0; y < g_term_height; ++y) {
    if (y == g_term_height - 1 && show_status) {
      std::array<char, 128> status{};
      int slen = snprintf(status.data(), status.size(),
                          "\x1b[%d;1H\x1b[0m\x1b[48;5;240m\x1b[38;5;255m "
                          "LinApple-TUI | F12: Quit ",
                          g_term_height);
      for (int i = 0; i < slen; ++i) {
        g_output_buffer.push_back(status.at(static_cast<size_t>(i)));
      }
      for (int i = slen - 10; i < g_term_width - 1; ++i) {
        g_output_buffer.push_back(' ');
      }
      continue;
    }

    std::array<char, 32> move_to{};
    int mlen = snprintf(move_to.data(), move_to.size(), "\x1b[%d;1H", y + 1);
    for (int i = 0; i < mlen; ++i) {
      g_output_buffer.push_back(move_to.at(static_cast<size_t>(i)));
    }

    for (int x = 0; x < g_term_width; ++x) {
      if (y == g_term_height - 1 && x == g_term_width - 1) {
        break;
      }
      TuiState_t& next =
          g_next_buffer.at(static_cast<size_t>(y * g_term_width + x));
      TuiState_t& prev =
          g_back_buffer.at(static_cast<size_t>(y * g_term_width + x));

      if (next != prev || g_frame_count % refresh_full_divisor == 0) {
        prev = next;
        if (next.fg != curr_fg) {
          std::array<char, 32> buf{};
          int l = snprintf(buf.data(), buf.size(), "\x1b[38;2;%d;%d;%dm",
                           next.fg.r, next.fg.g, next.fg.b);
          for (int i = 0; i < l; ++i) {
            g_output_buffer.push_back(buf.at(static_cast<size_t>(i)));
          }
          curr_fg = next.fg;
        }
        if (next.bg != curr_bg) {
          std::array<char, 32> buf{};
          int l = snprintf(buf.data(), buf.size(), "\x1b[48;2;%d;%d;%dm",
                           next.bg.r, next.bg.g, next.bg.b);
          for (int i = 0; i < l; ++i) {
            g_output_buffer.push_back(buf.at(static_cast<size_t>(i)));
          }
          curr_bg = next.bg;
        }
        for (int i = 0; i < utf8_glyph_size && next.glyph[i]; ++i) {
          g_output_buffer.push_back(static_cast<char>(next.glyph[i]));
        }
      } else {
        g_output_buffer.push_back('\x1b');
        g_output_buffer.push_back('[');
        g_output_buffer.push_back('C');
      }
    }
  }
  g_output_buffer.push_back('\x1b');
  g_output_buffer.push_back('[');
  g_output_buffer.push_back('0');
  g_output_buffer.push_back('m');
  write(STDOUT_FILENO, g_output_buffer.data(), g_output_buffer.size());
}
