// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>

struct TuiPixel {
  uint8_t r;
  uint8_t g;
  uint8_t b;

  constexpr auto operator==(const TuiPixel& other) const noexcept -> bool {
    return r == other.r && g == other.g && b == other.b;
  }
  constexpr auto operator!=(const TuiPixel& other) const noexcept -> bool {
    return !(*this == other);
  }
};

struct TuiState {
  std::array<uint8_t, 4> glyph;  // UTF-8 up to 4 bytes
  TuiPixel fg;
  TuiPixel bg;

  auto operator==(const TuiState& other) const noexcept -> bool {
    return glyph == other.glyph && fg == other.fg && bg == other.bg;
  }
  auto operator!=(const TuiState& other) const noexcept -> bool {
    return !(*this == other);
  }
};

auto tui_shape_detector_initialize() -> void;

auto tui_shape_detect_cell(const uint32_t* pixels, int pitch, int x_start,
                           int y_start, int x_end, int y_end,
                           TuiState* out_cell) -> void;
