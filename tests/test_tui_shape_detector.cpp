// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "doctest.h"
#include "frontends/tui/TuiShapeDetector.h"

namespace {

constexpr int k_cell_w = 14;
constexpr int k_cell_h = 16;
constexpr int k_fb_w = 560;
constexpr int k_fb_h = 384;
constexpr int k_pitch = k_fb_w * 4;
constexpr uint8_t k_white = 255;
constexpr int k_font_dim = 8;
constexpr int k_utf8_prefix_len = 3;

constexpr int k_h_line_y0 = 6;
constexpr int k_h_line_y1 = 9;
constexpr int k_v_line_x0 = 5;
constexpr int k_v_line_x1 = 8;
constexpr int k_mid_x = 9;

auto make_framebuffer() -> std::vector<uint32_t> {
  return std::vector<uint32_t>(static_cast<size_t>(k_fb_w * k_fb_h), 0);
}

auto set_pixel_rgb(std::vector<uint32_t>& fb, int x, int y, uint8_t r,
                   uint8_t g, uint8_t b) -> void {
  if (x >= 0 && x < k_fb_w && y >= 0 && y < k_fb_h) {
    fb.at(static_cast<size_t>(y * k_fb_w + x)) =
        (static_cast<uint32_t>(r)) | (static_cast<uint32_t>(g) << 8) |
        (static_cast<uint32_t>(b) << 16);
  }
}

auto draw_grid_line(std::vector<uint32_t>& fb, bool top, bool bottom, bool left,
                    bool right) -> void {
  if (left) {
    for (int y = k_h_line_y0; y <= k_h_line_y1; ++y) {
      for (int x = 0; x < k_mid_x; ++x) {
        set_pixel_rgb(fb, x, y, k_white, k_white, k_white);
      }
    }
  }
  if (right) {
    for (int y = k_h_line_y0; y <= k_h_line_y1; ++y) {
      for (int x = k_v_line_x0; x < k_cell_w; ++x) {
        set_pixel_rgb(fb, x, y, k_white, k_white, k_white);
      }
    }
  }
  if (top) {
    for (int y = 0; y <= k_h_line_y1; ++y) {
      for (int x = k_v_line_x0; x <= k_v_line_x1; ++x) {
        set_pixel_rgb(fb, x, y, k_white, k_white, k_white);
      }
    }
  }
  if (bottom) {
    for (int y = k_h_line_y0; y < k_cell_h; ++y) {
      for (int x = k_v_line_x0; x <= k_v_line_x1; ++x) {
        set_pixel_rgb(fb, x, y, k_white, k_white, k_white);
      }
    }
  }
}

auto draw_font_char(std::vector<uint32_t>& fb,
                    const std::array<uint8_t, k_font_dim>& rows,
                    uint8_t r = k_white, uint8_t g = k_white,
                    uint8_t b = k_white) -> void {
  for (int v = 0; v < k_font_dim; ++v) {
    for (int u = 0; u < k_font_dim; ++u) {
      bool is_on = (rows.at(static_cast<size_t>(v)) &
                    (1 << ((k_font_dim - 1) - u))) != 0;
      if (is_on) {
        int x0 = u * k_cell_w / k_font_dim;
        int x1 = (u + 1) * k_cell_w / k_font_dim;
        int y0 = v * k_cell_h / k_font_dim;
        int y1 = (v + 1) * k_cell_h / k_font_dim;
        for (int py = y0; py < y1; ++py) {
          for (int px = x0; px < x1; ++px) {
            set_pixel_rgb(fb, px, py, r, g, b);
          }
        }
      }
    }
  }
}

}  // namespace

TEST_CASE("TuiShapeDetector: Uniform Cells") {
  auto fb = make_framebuffer();
  TuiState_t cell{};

  // Case 1: All black
  tui_shape_detect_cell(fb.data(), k_pitch, 0, 0, k_cell_w, k_cell_h, &cell);
  CHECK(cell.glyph.at(0) == ' ');

  // Case 2: All white
  for (int y = 0; y < k_cell_h; ++y) {
    for (int x = 0; x < k_cell_w; ++x) {
      set_pixel_rgb(fb, x, y, k_white, k_white, k_white);
    }
  }
  tui_shape_detect_cell(fb.data(), k_pitch, 0, 0, k_cell_w, k_cell_h, &cell);
  CHECK(std::memcmp(cell.glyph.data(), "\xe2\x96\x88", k_utf8_prefix_len) == 0);
}

TEST_CASE("TuiShapeDetector: All 11 Box Drawing Shapes") {
  struct BoxTestCase_t {
    const char* expected;
    bool top;
    bool bottom;
    bool left;
    bool right;
  };

  const std::array<BoxTestCase_t, 11> test_cases = {{
      {"\xe2\x94\x80", false, false, true, true},  // ─
      {"\xe2\x94\x82", true, true, false, false},  // │
      {"\xe2\x94\x8c", false, true, false, true},  // ┌
      {"\xe2\x94\x90", false, true, true, false},  // ┐
      {"\xe2\x94\x94", true, false, false, true},  // └
      {"\xe2\x94\x98", true, false, true, false},  // ┘
      {"\xe2\x94\x9c", true, true, false, true},   // ├
      {"\xe2\x94\xa4", true, true, true, false},   // ┤
      {"\xe2\x94\xac", false, true, true, true},   // ┬
      {"\xe2\x94\xb4", true, false, true, true},   // ┴
      {"\xe2\x94\xbc", true, true, true, true},    // ┼
  }};

  for (const auto& tc : test_cases) {
    auto fb = make_framebuffer();
    TuiState_t cell{};
    draw_grid_line(fb, tc.top, tc.bottom, tc.left, tc.right);
    tui_shape_detect_cell(fb.data(), k_pitch, 0, 0, k_cell_w, k_cell_h, &cell);
    CHECK(std::memcmp(cell.glyph.data(), tc.expected, k_utf8_prefix_len) == 0);
  }
}

TEST_CASE("TuiShapeDetector: Apple II Font OCR Matching") {
  constexpr std::array<uint8_t, k_font_dim> font_5 = {0x7E, 0x60, 0x7C, 0x06,
                                                      0x06, 0x66, 0x3C, 0x00};
  constexpr std::array<uint8_t, k_font_dim> font_a = {0x18, 0x3C, 0x66, 0x66,
                                                      0x7E, 0x66, 0x66, 0x00};

  // Test '5'
  auto fb = make_framebuffer();
  TuiState_t cell{};
  draw_font_char(fb, font_5);
  tui_shape_detect_cell(fb.data(), k_pitch, 0, 0, k_cell_w, k_cell_h, &cell);
  CHECK(cell.glyph.at(0) == '5');

  // Test 'A'
  fb = make_framebuffer();
  draw_font_char(fb, font_a);
  tui_shape_detect_cell(fb.data(), k_pitch, 0, 0, k_cell_w, k_cell_h, &cell);
  CHECK(cell.glyph.at(0) == 'A');

  // Test Inverted '5' (white background, black text)
  fb = make_framebuffer();
  for (int y = 0; y < k_cell_h; ++y) {
    for (int x = 0; x < k_cell_w; ++x) {
      set_pixel_rgb(fb, x, y, k_white, k_white, k_white);
    }
  }
  draw_font_char(fb, font_5, 0, 0, 0);
  tui_shape_detect_cell(fb.data(), k_pitch, 0, 0, k_cell_w, k_cell_h, &cell);
  CHECK(cell.glyph.at(0) == '5');
}

TEST_CASE(
    "TuiShapeDetector: Quadrant Block Fallback for Custom Graphic Sprites") {
  auto fb = make_framebuffer();
  TuiState_t cell{};

  constexpr uint8_t r_col = 200;
  constexpr uint8_t g_col = 100;
  constexpr uint8_t b_col = 50;

  // Fill only Top-Left quadrant (x in [0, 7), y in [0, 8))
  for (int y = 0; y < k_cell_h / 2; ++y) {
    for (int x = 0; x < k_cell_w / 2; ++x) {
      set_pixel_rgb(fb, x, y, r_col, g_col, b_col);
    }
  }

  tui_shape_detect_cell(fb.data(), k_pitch, 0, 0, k_cell_w, k_cell_h, &cell);
  // Quadrant TL only is \xe2\x96\x98 (▘)
  CHECK(std::memcmp(cell.glyph.data(), "\xe2\x96\x98", k_utf8_prefix_len) == 0);

  // Fill Top-Half (Upper half block \xe2\x96\x80 ▀)
  fb = make_framebuffer();
  for (int y = 0; y < k_cell_h / 2; ++y) {
    for (int x = 0; x < k_cell_w; ++x) {
      set_pixel_rgb(fb, x, y, g_col, r_col, g_col);
    }
  }
  tui_shape_detect_cell(fb.data(), k_pitch, 0, 0, k_cell_w, k_cell_h, &cell);
  CHECK(std::memcmp(cell.glyph.data(), "\xe2\x96\x80", k_utf8_prefix_len) == 0);
}
