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

struct TuiPixel {
  uint8_t r, g, b;
  bool operator==(const TuiPixel& other) const {
    return r == other.r && g == other.g && b == other.b;
  }
  bool operator!=(const TuiPixel& other) const { return !(*this == other); }
};

struct TuiState {
  uint8_t glyph[4];  // UTF-8 up to 4 bytes
  TuiPixel fg;
  TuiPixel bg;
  bool operator==(const TuiState& other) const {
    return memcmp(glyph, other.glyph, 4) == 0 && fg == other.fg &&
           bg == other.bg;
  }
  bool operator!=(const TuiState& other) const { return !(*this == other); }
};

static int g_term_width = 0;
static int g_term_height = 0;
static std::vector<TuiState> g_back_buffer;
static std::vector<TuiState> g_next_buffer;
static std::vector<char> g_output_buffer;
static uint32_t g_frame_count = 0;

static constexpr int DEFAULT_TERM_WIDTH = 80;
static constexpr int DEFAULT_TERM_HEIGHT = 24;
static constexpr int UTF8_GLYPH_SIZE = 4;
static constexpr int OUTPUT_RESERVE_FACTOR = 64;
static constexpr int FLASH_DIVISOR = 15;
static constexpr int A2_PAGE1_ADDR = 0x400;
static constexpr int A2_PAGE2_OFFSET = 0x400;
static constexpr int A2_ZERO_PAGE_OFFSET = 0x00;
static constexpr int A2_CURSOR_X_ADDR = 0x24;
static constexpr int A2_CURSOR_Y_ADDR = 0x25;
static constexpr int A2_COLS_80 = 80;
static constexpr int A2_COLS_40 = 40;
static constexpr int MIN_TERM_HEIGHT_STATUS = 24;
static constexpr int A2_TEXT_ROWS = 24;
static constexpr int MIXED_MODE_TEXT_START = 20;
static constexpr int REFRESH_FULL_DIVISOR = 60;

auto TuiVideo_Initialize() -> void {
  printf("\x1b[?7l\x1b[?25l");
  fflush(stdout);
  TuiVideo_OnResize();
}

auto TuiVideo_OnResize() -> void {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
    g_term_width = w.ws_col;
    g_term_height = w.ws_row;
  } else {
    g_term_width = DEFAULT_TERM_WIDTH;
    g_term_height = DEFAULT_TERM_HEIGHT;
  }
  TuiState empty_cell{};
  memset(empty_cell.glyph, 0, UTF8_GLYPH_SIZE);
  empty_cell.glyph[0] = ' ';
  empty_cell.fg = {0, 0, 0};
  empty_cell.bg = {0, 0, 0};

  g_back_buffer.assign(static_cast<size_t>(g_term_width * g_term_height),
                       empty_cell);
  g_next_buffer.assign(static_cast<size_t>(g_term_width * g_term_height),
                       empty_cell);
  g_output_buffer.reserve(
      static_cast<size_t>(g_term_width * g_term_height * OUTPUT_RESERVE_FACTOR));
}

static auto GetTextAddr(int row, int col) -> uint16_t {
  static const std::array<uint16_t, A2_TEXT_ROWS> row_offsets = {
      0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
      0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0};
  return row_offsets.at(static_cast<size_t>(row)) + static_cast<uint16_t>(col);
}

static auto SetGlyph(TuiState& state, const char* str) -> void {
  memset(state.glyph, 0, UTF8_GLYPH_SIZE);
  strncpy(reinterpret_cast<char*>(state.glyph), str, UTF8_GLYPH_SIZE - 1);
}

auto TuiVideo_RenderFrame(const uint32_t* pixels, int width, int height,
                          int pitch) -> void {
  if (g_term_width <= 1 || g_term_height <= 1) {
    return;
  }
  g_frame_count++;
  bool flash_on = (g_frame_count / FLASH_DIVISOR) % 2 == 0;

  bool is_text_mode = VideoGetSWTEXT();
  bool is_mixed_mode = VideoGetSWMIXED();
  bool is_80col = VideoGetSW80COL();
  bool is_page2 = VideoGetSWPAGE2();
  bool alt_charset = VideoGetSWAltCharSet();
  uint16_t page_offset = is_page2 ? A2_PAGE2_OFFSET : 0x000;

  const uint8_t* zero_page = MemGetMainPtr(A2_ZERO_PAGE_OFFSET);
  int hw_cursor_x = zero_page[A2_CURSOR_X_ADDR];
  int hw_cursor_y = zero_page[A2_CURSOR_Y_ADDR];

  int a2_w_cols = is_80col ? A2_COLS_80 : A2_COLS_40;
  int avail_rows = g_term_height;
  bool show_status = (g_term_height > MIN_TERM_HEIGHT_STATUS);
  if (show_status) {
    avail_rows = g_term_height - 1;
  }

  TuiPixel a2_white = {255, 255, 255}, a2_black = {0, 0, 0};
  TuiPixel bg_letterbox = {10, 10, 10};

  int display_h = is_text_mode ? A2_TEXT_ROWS : (avail_rows);
  int display_w =
      is_text_mode ? (is_80col ? A2_COLS_80 : A2_COLS_40) : (avail_rows * 2 * 4 / 3);
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

      TuiState& cell =
          g_next_buffer.at(static_cast<size_t>(ty * g_term_width + tx));
      cell.bg = a2_black;

      if (is_text_mode || (is_mixed_mode && y >= display_h * MIXED_MODE_TEXT_START / A2_TEXT_ROWS)) {
        int r = is_text_mode ? y
                             : MIXED_MODE_TEXT_START + (y - display_h * MIXED_MODE_TEXT_START / A2_TEXT_ROWS) * 4 /
                                        (display_h * 4 / A2_TEXT_ROWS + 1);
        if (r > 23) {
          r = 23;
        }
        int c = x * a2_w_cols / display_w;

        uint8_t code = 0;
        if (is_80col) {
          if (c % 2 == 0) {
            code = *MemGetAuxPtr(static_cast<uint16_t>(A2_PAGE1_ADDR + page_offset +
                                                       GetTextAddr(r, c / 2)));
          } else {
            code = *MemGetMainPtr(static_cast<uint16_t>(A2_PAGE1_ADDR + page_offset +
                                                        GetTextAddr(r, c / 2)));
          }
        } else {
          code = *MemGetMainPtr(
              static_cast<uint16_t>(A2_PAGE1_ADDR + page_offset + GetTextAddr(r, c)));
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
            SetGlyph(cell, "\xe2\x96\x92"); // ▒ Checkerboard
            cell.fg = a2_white;
            cell.bg = a2_black;
          }
        }
      } else {
        int sx = x * width / display_w;
        int sy = y * height / display_h;
        SetGlyph(cell, "\xe2\x96\x80");
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
  TuiPixel curr_fg = {1, 1, 1}, curr_bg = {1, 1, 1};

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
      TuiState& next = g_next_buffer.at(static_cast<size_t>(y * g_term_width + x));
      TuiState& prev = g_back_buffer.at(static_cast<size_t>(y * g_term_width + x));

      if (next != prev || g_frame_count % REFRESH_FULL_DIVISOR == 0) {
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
        for (int i = 0; i < UTF8_GLYPH_SIZE && next.glyph[i]; ++i) {
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
