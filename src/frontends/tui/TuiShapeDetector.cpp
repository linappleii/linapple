// SPDX-License-Identifier: GPL-2.0-only
#include "TuiShapeDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr int subgrid_dim = 8;
constexpr int subgrid_pixels = subgrid_dim * subgrid_dim;
constexpr int max_char_mismatch = 4;
constexpr int max_pop_diff = 4;
constexpr int min_contrast_threshold = 35;
constexpr int dark_luminance_cutoff = 25;
constexpr int inverted_fg_threshold = subgrid_pixels / 2;
constexpr int quadrant_active_threshold = 7;
constexpr int quad_dim = 4;

constexpr int lum_weight_r = 299;
constexpr int lum_weight_g = 587;
constexpr int lum_weight_b = 114;
constexpr int lum_weight_sum = 1000;
constexpr uint8_t byte_mask = 0xFF;
constexpr uint8_t max_color_val = 255;
constexpr int max_bits = 64;
constexpr uint32_t green_shift = 8;
constexpr uint32_t blue_shift = 16;

struct GlyphPattern_t {
  char ch;
  uint64_t mask;
  uint8_t popcount;
};

// Standard Apple II 8x8 font patterns
constexpr std::array<GlyphPattern_t, 79> k_font_patterns = {{
    {'0', 0x003C66666E76663CULL, 30}, {'1', 0x007E181818181C18ULL, 19},
    {'2', 0x007E0C183060663CULL, 22}, {'3', 0x003C66603860663CULL, 23},
    {'4', 0x0020207E262C3830ULL, 19}, {'5', 0x003C6660603E067EULL, 25},
    {'6', 0x003C66663E060C38ULL, 24}, {'7', 0x000C0C0C1830607EULL, 18},
    {'8', 0x003C66663C66663CULL, 28}, {'9', 0x001C30607C66663CULL, 24},
    {'A', 0x0066667E66663C18ULL, 28}, {'B', 0x003E66663E66663EULL, 31},
    {'C', 0x003C66060606663CULL, 22}, {'D', 0x001E36666666361EULL, 28},
    {'E', 0x007E06061E06067EULL, 24}, {'F', 0x000606061E06067EULL, 20},
    {'G', 0x003C66667606663CULL, 27}, {'H', 0x006666667E666666ULL, 30},
    {'I', 0x003C18181818183CULL, 18}, {'J', 0x003C666060606070ULL, 19},
    {'K', 0x0066361E0E1E3666ULL, 27}, {'L', 0x007E060606060606ULL, 18},
    {'M', 0x00C6C6C6D6FEEEC6ULL, 34}, {'N', 0x006666767E7E6E66ULL, 34},
    {'O', 0x003C66666666663CULL, 28}, {'P', 0x000606063E66663EULL, 24},
    {'Q', 0x006C36566666663CULL, 28}, {'R', 0x00C666363E66663EULL, 30},
    {'S', 0x003C66603C06663CULL, 24}, {'T', 0x001818181818187EULL, 18},
    {'U', 0x003C666666666666ULL, 28}, {'V', 0x00183C6666666666ULL, 26},
    {'W', 0x00C6EEFED6C6C6C6ULL, 34}, {'X', 0x0066663C183C6666ULL, 26},
    {'Y', 0x001818183C666666ULL, 22}, {'Z', 0x007E060C1830607EULL, 22},
    {'a', 0x00DC667C603C0000ULL, 20}, {'b', 0x003E6666663E0606ULL, 26},
    {'c', 0x003C6606663C0000ULL, 18}, {'d', 0x007C6666667C6060ULL, 26},
    {'e', 0x003C067E663C0000ULL, 20}, {'f', 0x000C0C0C1E0C0C38ULL, 17},
    {'g', 0x3C607C66667C0000ULL, 24}, {'h', 0x00666666663E0606ULL, 25},
    {'i', 0x003C1818181C0018ULL, 15}, {'j', 0x1C36303030380030ULL, 18},
    {'k', 0x0066361E36660606ULL, 24}, {'l', 0x003C18181818181CULL, 17},
    {'m', 0x00C6C6D6FE660000ULL, 24}, {'n', 0x00666666663E0000ULL, 21},
    {'o', 0x003C6666663C0000ULL, 20}, {'p', 0x06063E66663E0000ULL, 22},
    {'q', 0x60607C66667C0000ULL, 22}, {'r', 0x00060606663E0000ULL, 15},
    {'s', 0x003E603C067C0000ULL, 18}, {'t', 0x00701818187E1818ULL, 19},
    {'u', 0x00DC666666660000ULL, 21}, {'v', 0x00183C6666660000ULL, 18},
    {'w', 0x006CEEFED6C60000ULL, 26}, {'x', 0x00663C183C660000ULL, 18},
    {'y', 0x3C607C6666660000ULL, 23}, {'z', 0x007E0C18307E0000ULL, 18},
    {'+', 0x000018187E181800ULL, 14}, {'-', 0x000000007E000000ULL, 6},
    {'*', 0x0000663CFF3C6600ULL, 24}, {'/', 0x0002060C18306040ULL, 12},
    {'=', 0x0000007E007E0000ULL, 12}, {'<', 0x0030180C060C1830ULL, 14},
    {'>', 0x000C18306030180CULL, 14}, {'?', 0x001800183060663CULL, 16},
    {'!', 0x0018001818181818ULL, 12}, {':', 0x0000181800181800ULL, 8},
    {';', 0x000C181800181800ULL, 10}, {'.', 0x0018180000000000ULL, 4},
    {',', 0x0C18180000000000ULL, 6},  {'(', 0x0030180C0C0C1830ULL, 14},
    {')', 0x000C18303030180CULL, 14}, {'[', 0x003C0C0C0C0C0C3CULL, 18},
    {']', 0x003C30303030303CULL, 18},
}};

static constexpr std::array<const char*, 16> k_quadrant_glyphs = {{
    " ",             // 0000
    "\xe2\x96\x97",  // 0001 ▗
    "\xe2\x96\x96",  // 0010 ▖
    "\xe2\x96\x84",  // 0011 ▄
    "\xe2\x96\x9d",  // 0100 ▝
    "\xe2\x96\x90",  // 0101 ▐
    "\xe2\x96\x9e",  // 0110 ▞
    "\xe2\x96\x9f",  // 0111 ▟
    "\xe2\x96\x98",  // 1000 ▘
    "\xe2\x96\x9a",  // 1001 ▚
    "\xe2\x96\x8c",  // 1010 ▌
    "\xe2\x96\x99",  // 1011 ▙
    "\xe2\x96\x80",  // 1100 ▀
    "\xe2\x96\x9c",  // 1101 ▜
    "\xe2\x96\x9b",  // 1110 ▛
    "\xe2\x96\x88",  // 1111 █
}};

static auto set_utf8_glyph(TuiState_t* cell, const char* str) -> void {
  cell->glyph.fill(0);
  if (str == nullptr) return;
  for (size_t i = 0; i < cell->glyph.size() - 1 && str[i] != '\0'; ++i) {
    cell->glyph.at(i) = static_cast<uint8_t>(str[i]);
  }
}

}  // namespace

auto tui_shape_detector_initialize() -> void {}

auto tui_shape_detect_cell(const uint32_t* pixels, int pitch, int x_start,
                           int y_start, int x_end, int y_end,
                           TuiState_t* out_cell) -> void {
  if (pixels == nullptr || out_cell == nullptr || x_end <= x_start ||
      y_end <= y_start) {
    return;
  }

  const int stride = pitch / static_cast<int>(sizeof(uint32_t));
  const int w_span = x_end - x_start;
  const int h_span = y_end - y_start;

  std::array<std::array<uint8_t, subgrid_dim>, subgrid_dim> lum_grid{};
  std::array<std::array<TuiPixel_t, subgrid_dim>, subgrid_dim> col_grid{};

  int min_lum = max_color_val;
  int max_lum = 0;
  int total_lum = 0;
  int total_r = 0;
  int total_g = 0;
  int total_b = 0;

  for (int v = 0; v < subgrid_dim; ++v) {
    int py = y_start + ((2 * v + 1) * h_span) / (2 * subgrid_dim);
    for (int u = 0; u < subgrid_dim; ++u) {
      int px = x_start + ((2 * u + 1) * w_span) / (2 * subgrid_dim);
      uint32_t pixel_val = pixels[static_cast<size_t>(py * stride + px)];
      auto r = static_cast<uint8_t>(pixel_val & byte_mask);
      auto g = static_cast<uint8_t>((pixel_val >> green_shift) & byte_mask);
      auto b = static_cast<uint8_t>((pixel_val >> blue_shift) & byte_mask);

      int lum = (r * lum_weight_r + g * lum_weight_g + b * lum_weight_b) /
                lum_weight_sum;
      lum_grid.at(static_cast<size_t>(v)).at(static_cast<size_t>(u)) =
          static_cast<uint8_t>(lum);
      col_grid.at(static_cast<size_t>(v)).at(static_cast<size_t>(u)) = {r, g,
                                                                        b};

      min_lum = std::min(min_lum, lum);
      max_lum = std::max(max_lum, lum);
      total_lum += lum;
      total_r += r;
      total_g += g;
      total_b += b;
    }
  }

  const int avg_lum = total_lum / subgrid_pixels;
  const auto avg_cell_color =
      TuiPixel_t{static_cast<uint8_t>(total_r / subgrid_pixels),
                 static_cast<uint8_t>(total_g / subgrid_pixels),
                 static_cast<uint8_t>(total_b / subgrid_pixels)};

  // Uniform solid or dark cell
  if (max_lum - min_lum < min_contrast_threshold) {
    if (avg_lum < dark_luminance_cutoff) {
      set_utf8_glyph(out_cell, " ");
      out_cell->fg = avg_cell_color;
      out_cell->bg = avg_cell_color;
    } else {
      set_utf8_glyph(out_cell, "\xe2\x96\x88");  // Full block
      out_cell->fg = avg_cell_color;
      out_cell->bg = {0, 0, 0};
    }
    return;
  }

  const int threshold = (min_lum + max_lum) / 2;
  uint64_t cell_bits = 0;
  int fg_count = 0;
  int bg_count = 0;
  int fg_r = 0, fg_g = 0, fg_b = 0;
  int bg_r = 0, bg_g = 0, bg_b = 0;

  for (int v = 0; v < subgrid_dim; ++v) {
    for (int u = 0; u < subgrid_dim; ++u) {
      const auto& pix =
          col_grid.at(static_cast<size_t>(v)).at(static_cast<size_t>(u));
      if (lum_grid.at(static_cast<size_t>(v)).at(static_cast<size_t>(u)) >=
          threshold) {
        cell_bits |= (1ULL << (v * subgrid_dim + u));
        fg_count++;
        fg_r += pix.r;
        fg_g += pix.g;
        fg_b += pix.b;
      } else {
        bg_count++;
        bg_r += pix.r;
        bg_g += pix.g;
        bg_b += pix.b;
      }
    }
  }

  auto fg_color = TuiPixel_t{
      static_cast<uint8_t>(fg_count > 0 ? (fg_r / fg_count) : max_color_val),
      static_cast<uint8_t>(fg_count > 0 ? (fg_g / fg_count) : max_color_val),
      static_cast<uint8_t>(fg_count > 0 ? (fg_b / fg_count) : max_color_val)};
  auto bg_color =
      TuiPixel_t{static_cast<uint8_t>(bg_count > 0 ? (bg_r / bg_count) : 0),
                 static_cast<uint8_t>(bg_count > 0 ? (bg_g / bg_count) : 0),
                 static_cast<uint8_t>(bg_count > 0 ? (bg_b / bg_count) : 0)};

  // Handle inverse presentation (light background, dark text)
  if (fg_count > inverted_fg_threshold) {
    cell_bits = ~cell_bits;
    std::swap(fg_color, bg_color);
  }

  const int pop = __builtin_popcountll(cell_bits);

  std::array<uint8_t, subgrid_dim> rows{};
  for (int v = 0; v < subgrid_dim; ++v) {
    rows.at(static_cast<size_t>(v)) =
        static_cast<uint8_t>((cell_bits >> (v * subgrid_dim)) & byte_mask);
  }

  constexpr uint8_t center_mask = 0x3C;
  constexpr uint8_t tl_mask = 0x03;
  constexpr uint8_t tr_mask = 0xC0;

  const bool has_n = (rows.at(0) & center_mask) != 0 &&
                     (rows.at(1) & center_mask) != 0 &&
                     (rows.at(2) & center_mask) != 0;
  const bool has_s = (rows.at(5) & center_mask) != 0 &&
                     (rows.at(6) & center_mask) != 0 &&
                     (rows.at(7) & center_mask) != 0;

  const uint8_t mid_row_mask =
      rows.at(2) | rows.at(3) | rows.at(4) | rows.at(5);
  const bool has_w = (mid_row_mask & 0x01) != 0 && (mid_row_mask & 0x02) != 0 &&
                     (mid_row_mask & 0x04) != 0;
  const bool has_e = (mid_row_mask & 0x20) != 0 && (mid_row_mask & 0x40) != 0 &&
                     (mid_row_mask & 0x80) != 0;
  const bool has_c =
      (rows.at(3) & center_mask) != 0 || (rows.at(4) & center_mask) != 0;

  constexpr size_t row_top_0 = 0;
  constexpr size_t row_top_1 = 1;
  constexpr size_t row_bot_6 = 6;
  constexpr size_t row_bot_7 = 7;

  int corner_count = 0;
  if ((rows.at(row_top_0) & tl_mask) != 0 ||
      (rows.at(row_top_1) & tl_mask) != 0) {
    corner_count++;
  }
  if ((rows.at(row_top_0) & tr_mask) != 0 ||
      (rows.at(row_top_1) & tr_mask) != 0) {
    corner_count++;
  }
  if ((rows.at(row_bot_6) & tl_mask) != 0 ||
      (rows.at(row_bot_7) & tl_mask) != 0) {
    corner_count++;
  }
  if ((rows.at(row_bot_6) & tr_mask) != 0 ||
      (rows.at(row_bot_7) & tr_mask) != 0) {
    corner_count++;
  }

  const int branch_count =
      (has_n ? 1 : 0) + (has_s ? 1 : 0) + (has_w ? 1 : 0) + (has_e ? 1 : 0);

  if (has_c && corner_count == 0 && branch_count >= 2) {
    if (has_w && has_e && !has_n && !has_s) {
      set_utf8_glyph(out_cell, "\xe2\x94\x80");  // ─
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
    if (has_n && has_s && !has_w && !has_e) {
      set_utf8_glyph(out_cell, "\xe2\x94\x82");  // │
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
    if (has_s && has_e && !has_n && !has_w) {
      set_utf8_glyph(out_cell, "\xe2\x94\x8c");  // ┌
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
    if (has_s && has_w && !has_n && !has_e) {
      set_utf8_glyph(out_cell, "\xe2\x94\x90");  // ┐
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
    if (has_n && has_e && !has_s && !has_w) {
      set_utf8_glyph(out_cell, "\xe2\x94\x94");  // └
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
    if (has_n && has_w && !has_s && !has_e) {
      set_utf8_glyph(out_cell, "\xe2\x94\x98");  // ┘
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
    if (has_n && has_s && has_e && !has_w) {
      set_utf8_glyph(out_cell, "\xe2\x94\x9c");  // ├
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
    if (has_n && has_s && has_w && !has_e) {
      set_utf8_glyph(out_cell, "\xe2\x94\xa4");  // ┤
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
    if (has_s && has_e && has_w && !has_n) {
      set_utf8_glyph(out_cell, "\xe2\x94\xac");  // ┬
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
    if (has_n && has_e && has_w && !has_s) {
      set_utf8_glyph(out_cell, "\xe2\x94\xb4");  // ┴
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
    if (has_n && has_s && has_e && has_w) {
      set_utf8_glyph(out_cell, "\xe2\x94\xbc");  // ┼
      out_cell->fg = fg_color;
      out_cell->bg = bg_color;
      return;
    }
  }

  // --- Tier 2: Apple II Font OCR Matching ---
  int min_distance = max_bits;
  char best_char = '\0';
  int best_pop_diff = max_bits;

  for (const auto& pat : k_font_patterns) {
    const int dist = __builtin_popcountll(cell_bits ^ pat.mask);
    const int pop_diff = std::abs(pop - static_cast<int>(pat.popcount));
    if (dist < min_distance) {
      min_distance = dist;
      best_char = pat.ch;
      best_pop_diff = pop_diff;
    }
  }

  if (min_distance <= max_char_mismatch && best_pop_diff <= max_pop_diff) {
    out_cell->glyph.fill(0);
    out_cell->glyph.at(0) = static_cast<uint8_t>(best_char);
    out_cell->fg = fg_color;
    out_cell->bg = bg_color;
    return;
  }

  // --- Tier 3: 2x2 Quadrant High-Resolution Block Fallback ---
  int q_tl = 0, q_tr = 0, q_bl = 0, q_br = 0;
  for (int v = 0; v < subgrid_dim; ++v) {
    for (int u = 0; u < subgrid_dim; ++u) {
      const bool is_set = (cell_bits & (1ULL << (v * subgrid_dim + u))) != 0;
      if (!is_set) continue;
      if (v < quad_dim && u < quad_dim) {
        q_tl++;
      } else if (v < quad_dim && u >= quad_dim) {
        q_tr++;
      } else if (v >= quad_dim && u < quad_dim) {
        q_bl++;
      } else {
        q_br++;
      }
    }
  }

  const auto quad_mask =
      static_cast<uint8_t>(((q_tl >= quadrant_active_threshold ? 1 : 0) << 3) |
                           ((q_tr >= quadrant_active_threshold ? 1 : 0) << 2) |
                           ((q_bl >= quadrant_active_threshold ? 1 : 0) << 1) |
                           (q_br >= quadrant_active_threshold ? 1 : 0));

  set_utf8_glyph(out_cell, k_quadrant_glyphs.at(quad_mask));
  out_cell->fg = fg_color;
  out_cell->bg = bg_color;
}
