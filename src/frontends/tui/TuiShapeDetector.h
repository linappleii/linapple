// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct TuiPixel_t {
  uint8_t r;
  uint8_t g;
  uint8_t b;

  auto operator==(const TuiPixel_t& other) const -> bool {
    return r == other.r && g == other.g && b == other.b;
  }
  auto operator!=(const TuiPixel_t& other) const -> bool {
    return !(*this == other);
  }
};

struct TuiState_t {
  std::array<uint8_t, 4> glyph;  // UTF-8 up to 4 bytes
  TuiPixel_t fg;
  TuiPixel_t bg;

  auto operator==(const TuiState_t& other) const -> bool {
    return glyph == other.glyph && fg == other.fg && bg == other.bg;
  }
  auto operator!=(const TuiState_t& other) const -> bool {
    return !(*this == other);
  }
};

auto tui_shape_detector_initialize() -> void;

auto tui_shape_detect_cell(const uint32_t* pixels, int pitch, int x_start,
                           int y_start, int x_end, int y_end,
                           TuiState_t* out_cell) -> void;
