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
  g_output_buffer.reserve(
      static_cast<size_t>(g_term_width * g_term_height * output_reserve_factor));
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
  strncpy(reinterpret_cast<char*>(state.glyph), str, utf8_glyph_size - 1);
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

  const uint8_t* zero_page = mem_get_main_ptr(a2_zero_page_offset);
  int hw_cursor_x = zero_page[a2_cursor_x_addr];
  int hw_cursor_y = zero_page[a2_cursor_y_addr];

  int a2_w_cols = is_80col ? a2_cols_80 : a2_cols_40;
  int avail_rows = g_term_height;
  bool show_status = (g_term_height > min_term_height_status);
  if (show_status) {
    avail_rows = g_term_height - 1;
  }

  TuiPixel_t a2_white = {255, 255, 255}, a2_black = {0, 0, 0};
  TuiPixel_t bg_letterbox = {10, 10, 10};

  int display_h = is_text_mode ? a2_text_rows : (avail_rows);
  int display_w =
      is_text_mode ? (is_80col ? a2_cols_80 : a2_cols_40) : (avail_rows * 2 * 4 / 3);
  if (display_w > g_term_width) {
    display_w = g_term_width;
    if (!is_text_mode) {
      display_h = display_w * 3 / 8;
    }
  }

  int off_x = (g_term_width - display_w) / 2;
  int off_y = (avail_rows - display_h) / 2;
  if (off_y < 0) {
    off_y = 0;
  }

  // Reset next buffer with letterbox color
  for (auto& cell : g_next_buffer) {
    cell.glyph[0] = ' ';
    cell.glyph[1] = 0;
    cell.fg = {40, 40, 40};
    cell.bg = bg_letterbox;
  }

  for (int y = 0; y < display_h; ++y) {
    int ty = y + off_y;
    if (ty >= avail_rows) {
      break;
    }

    for (int x = 0; x < display_w; ++x) {
      int tx = x + off_x;
      if (tx >= g_term_width) {
        break;
      }

      TuiState_t& cell =
          g_next_buffer.at(static_cast<size_t>(ty * g_term_width + tx));
      cell.bg = a2_black;

      if (is_text_mode || (is_mixed_mode && y >= display_h * mixed_mode_text_start / a2_text_rows)) {
        int r = is_text_mode ? y
                             : mixed_mode_text_start + (y - display_h * mixed_mode_text_start / a2_text_rows) * 4 /
                                        (display_h * 4 / a2_text_rows + 1);
        if (r > 23) {
          r = 23;
        }
        int c = x * a2_w_cols / display_w;

        uint8_t code = 0;
        if (is_80col) {
          if (c % 2 == 0) {
            code = *mem_get_aux_ptr(static_cast<uint16_t>(a2_page1_addr + page_offset +
                                                       get_text_addr(r, c / 2)));
          } else {
            code = *mem_get_main_ptr(static_cast<uint16_t>(a2_page1_addr + page_offset +
                                                        get_text_addr(r, c / 2)));
          }
        } else {
          code = *mem_get_main_ptr(
              static_cast<uint16_t>(a2_page1_addr + page_offset + get_text_addr(r, c)));
        }

        uint8_t ascii = 0, attr = 0;
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

        cell.glyph[0] = (ascii < 32 || ascii > 126) ? ' ' : ascii;
        cell.glyph[1] = 0;
        cell.fg = a2_white;
        cell.bg = a2_black;
        if (attr == 2 || (attr == 3 && !flash_on)) {
          cell.fg = a2_black;
          cell.bg = a2_white;
        }

        if (r == hw_cursor_y && c == hw_cursor_x) {
          if (flash_on) {
            set_glyph(cell, "\xe2\x96\x92"); // ▒ Checkerboard
            cell.fg = a2_white;
            cell.bg = a2_black;
          }
        }
      } else {
        int sx = x * width / display_w;
        int sy = y * height / display_h;
        set_glyph(cell, "\xe2\x96\x80");
        uint32_t p = pixels[static_cast<size_t>(sy) * (static_cast<size_t>(pitch) / 4) + static_cast<size_t>(sx)];
        cell.fg = {static_cast<uint8_t>(p & 0xFF),
                   static_cast<uint8_t>((p >> 8) & 0xFF),
                   static_cast<uint8_t>((p >> 16) & 0xFF)};
        // For simplicity, using same color for BG in half-block when sampling point
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
      TuiState_t& next = g_next_buffer.at(static_cast<size_t>(y * g_term_width + x));
      TuiState_t& prev = g_back_buffer.at(static_cast<size_t>(y * g_term_width + x));

      if (next != prev || g_frame_count % refresh_full_divisor == 0) {
        prev = next;
        if (next.fg != curr_fg) {
          std::array<char, 32> buf{};
          int l = snprintf(buf.data(), buf.size(), "\x1b[38;2;%d;%d;%dm", next.fg.r, next.fg.g,
                          next.fg.b);
          for (int i = 0; i < l; ++i) {
            g_output_buffer.push_back(buf.at(static_cast<size_t>(i)));
          }
          curr_fg = next.fg;
        }
        if (next.bg != curr_bg) {
          std::array<char, 32> buf{};
          int l = snprintf(buf.data(), buf.size(), "\x1b[48;2;%d;%d;%dm", next.bg.r, next.bg.g,
                          next.bg.b);
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
