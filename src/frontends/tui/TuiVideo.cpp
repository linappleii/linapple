// SPDX-License-Identifier: GPL-2.0-only
#include "TuiVideo.h"

#include <asm-generic/ioctls.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Debugger/Debugger_Display.h"
#include "TuiDiskSelect.h"
#include "TuiShapeDetector.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/FileBrowser.h"
#include "frontends/common/HelpText.h"
#include "frontends/common/VideoSurface.h"

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

static TuiRenderMode_t g_render_mode = TUI_RENDER_SMART;
static bool g_show_help = false;

auto tui_video_set_render_mode(TuiRenderMode_t mode) -> void {
  g_render_mode = mode;
}

auto tui_video_toggle_render_mode() -> void {
  g_render_mode =
      (g_render_mode == TUI_RENDER_SMART) ? TUI_RENDER_BLOCK : TUI_RENDER_SMART;
}

auto tui_video_get_render_mode() -> TuiRenderMode_t { return g_render_mode; }

static bool g_fullscreen = false;

auto tui_video_toggle_help() -> void { g_show_help = !g_show_help; }

auto tui_video_is_help_visible() -> bool { return g_show_help; }

auto tui_video_close_help() -> void { g_show_help = false; }

auto tui_video_toggle_fullscreen() -> void {
  g_fullscreen = !g_fullscreen;
  g_state.fullscreen = g_fullscreen;
}

auto tui_video_is_fullscreen() -> bool { return g_fullscreen; }

static auto set_glyph(TuiState_t& state, const char* str) -> void {
  state.glyph.fill(0);
  if (str == nullptr) return;
  for (size_t i = 0; i < state.glyph.size() - 1 && str[i] != '\0'; ++i) {
    state.glyph.at(i) = static_cast<uint8_t>(str[i]);
  }
}

static auto render_help_overlay() -> void {
  constexpr int box_inner_w = 64;
  constexpr int box_w = box_inner_w + 2;

  // Features not supported in the TUI are excluded from the help overlay.
  constexpr std::array<HelpFeature_t, 1> excluded = {
      {HelpFeature_t::numpad_speed}};

  const bool compact = (g_term_height < 28);
  std::vector<const char*> visible_body_lines;
  visible_body_lines.reserve(HELP_BODY_LINES.size());
  for (const HelpLine_t& line : HELP_BODY_LINES) {
    if (compact && line.feature == HelpFeature_t::separator) {
      continue;
    }
    bool skip = false;
    for (HelpFeature_t ex : excluded) {
      if (line.feature == ex) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }
    visible_body_lines.push_back(line.text);
  }

  const int box_h =
      static_cast<int>(HELP_HEADER_STRINGS.size() + visible_body_lines.size()) +
      3;

  if (g_term_width < box_w || g_term_height < 20) {
    return;
  }

  const int start_x = (g_term_width - box_w) / 2;
  const int start_y =
      (g_term_height > box_h) ? (g_term_height - 1 - box_h) / 2 : 0;

  const TuiPixel_t header_border = {255, 255, 0};  // Yellow header box
  const TuiPixel_t header_fg = {255, 255, 100};    // Bright Yellow text
  const TuiPixel_t body_border = {255, 255, 255};  // White body box
  const TuiPixel_t body_fg = {240, 240, 240};      // Soft White text
  const TuiPixel_t modal_bg = {10, 15, 25};  // Dimmed Blue-Black background

  // Draw Header Box (Top Border)
  {
    auto& tl =
        g_next_buffer.at(static_cast<size_t>(start_y * g_term_width + start_x));
    set_glyph(tl, "\xe2\x94\x8c");  // ┌
    tl.fg = header_border;
    tl.bg = modal_bg;

    for (int x = 1; x <= box_inner_w; ++x) {
      auto& cell = g_next_buffer.at(
          static_cast<size_t>(start_y * g_term_width + start_x + x));
      set_glyph(cell, "\xe2\x94\x80");  // ─
      cell.fg = header_border;
      cell.bg = modal_bg;
    }

    auto& tr = g_next_buffer.at(
        static_cast<size_t>(start_y * g_term_width + start_x + box_w - 1));
    set_glyph(tr, "\xe2\x94\x90");  // ┐
    tr.fg = header_border;
    tr.bg = modal_bg;
  }

  // Draw Header Lines (centered, Yellow)
  for (size_t row_idx = 0; row_idx < HELP_HEADER_STRINGS.size(); ++row_idx) {
    int cur_y = start_y + 1 + static_cast<int>(row_idx);
    if (cur_y >= g_term_height) break;

    auto& left_border =
        g_next_buffer.at(static_cast<size_t>(cur_y * g_term_width + start_x));
    set_glyph(left_border, "\xe2\x94\x82");  // │
    left_border.fg = header_border;
    left_border.bg = modal_bg;

    const char* line = HELP_HEADER_STRINGS.at(row_idx);
    int line_len = static_cast<int>(strlen(line));
    int pad = (box_inner_w > line_len) ? (box_inner_w - line_len) : 0;
    int l_pad = pad / 2;

    for (int col_idx = 0; col_idx < box_inner_w; ++col_idx) {
      auto& cell = g_next_buffer.at(
          static_cast<size_t>(cur_y * g_term_width + start_x + 1 + col_idx));
      cell.glyph.fill(0);
      if (col_idx >= l_pad && col_idx < l_pad + line_len) {
        cell.glyph.at(0) = static_cast<uint8_t>(line[col_idx - l_pad]);
      } else {
        cell.glyph.at(0) = ' ';
      }
      cell.fg = header_fg;
      cell.bg = modal_bg;
    }

    auto& right_border = g_next_buffer.at(
        static_cast<size_t>(cur_y * g_term_width + start_x + box_w - 1));
    set_glyph(right_border, "\xe2\x94\x82");  // │
    right_border.fg = header_border;
    right_border.bg = modal_bg;
  }

  // Draw Divider (between Header and Body)
  {
    int div_y = start_y + 1 + static_cast<int>(HELP_HEADER_STRINGS.size());
    if (div_y < g_term_height) {
      auto& div_l =
          g_next_buffer.at(static_cast<size_t>(div_y * g_term_width + start_x));
      set_glyph(div_l, "\xe2\x94\x9c");  // ├
      div_l.fg = body_border;
      div_l.bg = modal_bg;

      for (int x = 1; x <= box_inner_w; ++x) {
        auto& cell = g_next_buffer.at(
            static_cast<size_t>(div_y * g_term_width + start_x + x));
        set_glyph(cell, "\xe2\x94\x80");  // ─
        cell.fg = body_border;
        cell.bg = modal_bg;
      }

      auto& div_r = g_next_buffer.at(
          static_cast<size_t>(div_y * g_term_width + start_x + box_w - 1));
      set_glyph(div_r, "\xe2\x94\xa4");  // ┤
      div_r.fg = body_border;
      div_r.bg = modal_bg;
    }
  }

  // Draw Body Lines (White text, left aligned with margin)
  for (size_t row_idx = 0; row_idx < visible_body_lines.size(); ++row_idx) {
    int cur_y =
        start_y + 2 + static_cast<int>(HELP_HEADER_STRINGS.size() + row_idx);
    if (cur_y >= g_term_height) break;

    auto& left_border =
        g_next_buffer.at(static_cast<size_t>(cur_y * g_term_width + start_x));
    set_glyph(left_border, "\xe2\x94\x82");  // │
    left_border.fg = body_border;
    left_border.bg = modal_bg;

    const char* line = visible_body_lines.at(row_idx);
    size_t line_len = strlen(line);

    for (int col_idx = 0; col_idx < box_inner_w; ++col_idx) {
      auto& cell = g_next_buffer.at(
          static_cast<size_t>(cur_y * g_term_width + start_x + 1 + col_idx));
      cell.glyph.fill(0);
      if (col_idx >= 2 && static_cast<size_t>(col_idx - 2) < line_len) {
        cell.glyph.at(0) = static_cast<uint8_t>(line[col_idx - 2]);
      } else {
        cell.glyph.at(0) = ' ';
      }
      cell.fg = body_fg;
      cell.bg = modal_bg;
    }

    auto& right_border = g_next_buffer.at(
        static_cast<size_t>(cur_y * g_term_width + start_x + box_w - 1));
    set_glyph(right_border, "\xe2\x94\x82");  // │
    right_border.fg = body_border;
    right_border.bg = modal_bg;
  }

  // Draw Bottom Border
  {
    int bot_y = start_y + box_h - 1;
    if (bot_y < g_term_height) {
      auto& bl =
          g_next_buffer.at(static_cast<size_t>(bot_y * g_term_width + start_x));
      set_glyph(bl, "\xe2\x94\x94");  // └
      bl.fg = body_border;
      bl.bg = modal_bg;

      for (int x = 1; x <= box_inner_w; ++x) {
        auto& cell = g_next_buffer.at(
            static_cast<size_t>(bot_y * g_term_width + start_x + x));
        set_glyph(cell, "\xe2\x94\x80");  // ─
        cell.fg = body_border;
        cell.bg = modal_bg;
      }

      auto& br = g_next_buffer.at(
          static_cast<size_t>(bot_y * g_term_width + start_x + box_w - 1));
      set_glyph(br, "\xe2\x94\x98");  // ┘
      br.fg = body_border;
      br.bg = modal_bg;
    }
  }
}

static auto render_disk_select_overlay() -> void {
  if (!tui_disk_select_is_active()) return;

  const FileList_t* list = tui_disk_select_get_file_list();
  const size_t total_count = list ? file_browser_get_count(list) : 0;
  const size_t selected_idx = tui_disk_select_get_selected_index();
  const size_t first_vis = tui_disk_select_get_first_visible_index();
  const int slot = tui_disk_select_get_slot();
  const int drive = tui_disk_select_get_drive();
  const char* cur_dir = tui_disk_select_get_current_dir();

  int box_inner_w = 66;
  if (box_inner_w > g_term_width - 4) {
    box_inner_w = g_term_width - 4;
  }
  if (box_inner_w < 40) return;

  const int box_w = box_inner_w + 2;

  int max_visible_rows = 14;
  if (g_term_height < 24) {
    max_visible_rows = g_term_height - 10;
    if (max_visible_rows < 4) max_visible_rows = 4;
  } else if (g_term_height > 30) {
    max_visible_rows = g_term_height - 12;
  }

  const int box_h = max_visible_rows + 8;
  if (g_term_height < box_h) return;

  const int start_x = (g_term_width - box_w) / 2;
  const int start_y =
      (g_term_height > box_h) ? (g_term_height - 1 - box_h) / 2 : 0;

  const TuiPixel_t border_color = {255, 255, 0};  // Yellow border (SDL parity)
  const TuiPixel_t header_fg = {255, 255, 100};   // Bright Yellow text
  const TuiPixel_t hint_fg = {200, 200, 200};     // Soft white hint
  const TuiPixel_t item_fg = {230, 230, 230};     // White item text
  const TuiPixel_t dir_fg = {100, 220, 255};      // Cyan directory text
  const TuiPixel_t sel_bg = {50, 90, 170};        // Blue selection background
  const TuiPixel_t sel_fg = {255, 255, 255};      // Bright white selected text
  const TuiPixel_t modal_bg = {10, 15, 25};       // Dimmed dark background
  const TuiPixel_t size_fg = {180, 180, 180};     // Gray size info

  // 1. Draw Top Border: ┌───┐
  {
    auto& tl =
        g_next_buffer.at(static_cast<size_t>(start_y * g_term_width + start_x));
    set_glyph(tl, "\xe2\x94\x8c");
    tl.fg = border_color;
    tl.bg = modal_bg;

    for (int x = 1; x <= box_inner_w; ++x) {
      auto& cell = g_next_buffer.at(
          static_cast<size_t>(start_y * g_term_width + start_x + x));
      set_glyph(cell, "\xe2\x94\x80");
      cell.fg = border_color;
      cell.bg = modal_bg;
    }

    auto& tr = g_next_buffer.at(
        static_cast<size_t>(start_y * g_term_width + start_x + box_w - 1));
    set_glyph(tr, "\xe2\x94\x90");
    tr.fg = border_color;
    tr.bg = modal_bg;
  }

  // 2. Draw Header Lines
  std::array<std::string, 3> header_lines;
  header_lines[0] = cur_dir;
  header_lines[1] = std::string(disk_browser_get_title(slot)) +
                    (drive == 0 ? " [Drive 1]" : " [Drive 2]");
  header_lines[2] = "Press ENTER to choose, or ESC to cancel";

  for (size_t row_idx = 0; row_idx < 3; ++row_idx) {
    int cur_y = start_y + 1 + static_cast<int>(row_idx);
    if (cur_y >= g_term_height) break;

    auto& lb =
        g_next_buffer.at(static_cast<size_t>(cur_y * g_term_width + start_x));
    set_glyph(lb, "\xe2\x94\x82");
    lb.fg = border_color;
    lb.bg = modal_bg;

    const std::string& text = header_lines.at(row_idx);
    int line_len = static_cast<int>(text.size());
    int pad = (box_inner_w > line_len) ? (box_inner_w - line_len) : 0;
    int l_pad = pad / 2;

    for (int col_idx = 0; col_idx < box_inner_w; ++col_idx) {
      auto& cell = g_next_buffer.at(
          static_cast<size_t>(cur_y * g_term_width + start_x + 1 + col_idx));
      cell.glyph.fill(0);
      if (col_idx >= l_pad && col_idx < l_pad + line_len) {
        cell.glyph.at(0) =
            static_cast<uint8_t>(text[static_cast<size_t>(col_idx - l_pad)]);
      } else {
        cell.glyph.at(0) = ' ';
      }
      cell.fg = (row_idx < 2) ? header_fg : hint_fg;
      cell.bg = modal_bg;
    }

    auto& rb = g_next_buffer.at(
        static_cast<size_t>(cur_y * g_term_width + start_x + box_w - 1));
    set_glyph(rb, "\xe2\x94\x82");
    rb.fg = border_color;
    rb.bg = modal_bg;
  }

  // 3. Top Divider: ├───┤
  {
    int div_y = start_y + 4;
    auto& div_l =
        g_next_buffer.at(static_cast<size_t>(div_y * g_term_width + start_x));
    set_glyph(div_l, "\xe2\x94\x9c");
    div_l.fg = border_color;
    div_l.bg = modal_bg;

    for (int x = 1; x <= box_inner_w; ++x) {
      auto& cell = g_next_buffer.at(
          static_cast<size_t>(div_y * g_term_width + start_x + x));
      set_glyph(cell, "\xe2\x94\x80");
      cell.fg = border_color;
      cell.bg = modal_bg;
    }

    if (first_vis > 0 && box_inner_w >= 10) {
      auto& cell = g_next_buffer.at(static_cast<size_t>(
          div_y * g_term_width + start_x + (box_inner_w / 2)));
      set_glyph(cell, "\xe2\x96\xb2");  // ▲
      cell.fg = header_fg;
    }

    auto& div_r = g_next_buffer.at(
        static_cast<size_t>(div_y * g_term_width + start_x + box_w - 1));
    set_glyph(div_r, "\xe2\x94\xa4");
    div_r.fg = border_color;
    div_r.bg = modal_bg;
  }

  // 4. Draw File List Rows
  for (int row = 0; row < max_visible_rows; ++row) {
    int cur_y = start_y + 5 + row;
    if (cur_y >= g_term_height) break;

    auto& lb =
        g_next_buffer.at(static_cast<size_t>(cur_y * g_term_width + start_x));
    set_glyph(lb, "\xe2\x94\x82");
    lb.fg = border_color;
    lb.bg = modal_bg;

    size_t item_idx = first_vis + static_cast<size_t>(row);
    const FileEntry_t* entry = (item_idx < total_count)
                                   ? file_browser_get_entry(list, item_idx)
                                   : nullptr;
    const bool is_selected = (entry != nullptr && item_idx == selected_idx);

    char size_str[32] = {};
    if (entry != nullptr) {
      file_entry_format_type_or_size(entry, size_str, sizeof(size_str));
    }

    std::string name_str = entry ? entry->name : "";
    int name_max_w = box_inner_w - 14;
    if (static_cast<int>(name_str.size()) > name_max_w) {
      name_str =
          name_str.substr(0, static_cast<size_t>(name_max_w - 3)) + "...";
    }

    TuiPixel_t row_bg = is_selected ? sel_bg : modal_bg;
    TuiPixel_t row_fg =
        is_selected
            ? sel_fg
            : (entry && file_entry_is_dir_type(entry) ? dir_fg : item_fg);

    for (int col_idx = 0; col_idx < box_inner_w; ++col_idx) {
      auto& cell = g_next_buffer.at(
          static_cast<size_t>(cur_y * g_term_width + start_x + 1 + col_idx));
      cell.glyph.fill(0);
      cell.bg = row_bg;
      cell.fg = row_fg;

      if (entry == nullptr) {
        cell.glyph.at(0) = ' ';
      } else if (col_idx == 1 && is_selected) {
        set_glyph(cell, "\xe2\x96\xb6");  // ▶ cursor
        cell.fg = {255, 255, 100};
      } else if (col_idx >= 3 &&
                 col_idx < 3 + static_cast<int>(name_str.size())) {
        cell.glyph.at(0) =
            static_cast<uint8_t>(name_str.at(static_cast<size_t>(col_idx - 3)));
      } else {
        int right_pos = box_inner_w - 2 - static_cast<int>(strlen(size_str));
        if (col_idx >= right_pos &&
            col_idx < right_pos + static_cast<int>(strlen(size_str))) {
          cell.glyph.at(0) =
              static_cast<uint8_t>(size_str[col_idx - right_pos]);
          if (!is_selected) cell.fg = size_fg;
        } else {
          cell.glyph.at(0) = ' ';
        }
      }
    }

    auto& rb = g_next_buffer.at(
        static_cast<size_t>(cur_y * g_term_width + start_x + box_w - 1));
    set_glyph(rb, "\xe2\x94\x82");
    rb.fg = border_color;
    rb.bg = modal_bg;
  }

  // 5. Bottom Divider: ├───┤
  {
    int div_y = start_y + 5 + max_visible_rows;
    auto& div_l =
        g_next_buffer.at(static_cast<size_t>(div_y * g_term_width + start_x));
    set_glyph(div_l, "\xe2\x94\x9c");
    div_l.fg = border_color;
    div_l.bg = modal_bg;

    for (int x = 1; x <= box_inner_w; ++x) {
      auto& cell = g_next_buffer.at(
          static_cast<size_t>(div_y * g_term_width + start_x + x));
      set_glyph(cell, "\xe2\x94\x80");
      cell.fg = border_color;
      cell.bg = modal_bg;
    }

    if (first_vis + static_cast<size_t>(max_visible_rows) < total_count &&
        box_inner_w >= 10) {
      auto& cell = g_next_buffer.at(static_cast<size_t>(
          div_y * g_term_width + start_x + (box_inner_w / 2)));
      set_glyph(cell, "\xe2\x96\xbc");  // ▼
      cell.fg = header_fg;
    }

    auto& div_r = g_next_buffer.at(
        static_cast<size_t>(div_y * g_term_width + start_x + box_w - 1));
    set_glyph(div_r, "\xe2\x94\xa4");
    div_r.fg = border_color;
    div_r.bg = modal_bg;
  }

  // 6. Footer Line
  {
    int cur_y = start_y + 6 + max_visible_rows;
    if (cur_y < g_term_height) {
      auto& lb =
          g_next_buffer.at(static_cast<size_t>(cur_y * g_term_width + start_x));
      set_glyph(lb, "\xe2\x94\x82");
      lb.fg = border_color;
      lb.bg = modal_bg;

      const std::string footer_text =
          "\xe2\x86\x91/\xe2\x86\x93: Select | Enter: Open | Esc: Cancel | "
          "A-Z: Jump";
      int line_len = static_cast<int>(footer_text.size());
      int pad = (box_inner_w > line_len) ? (box_inner_w - line_len) : 0;
      int l_pad = pad / 2;

      for (int col_idx = 0; col_idx < box_inner_w; ++col_idx) {
        auto& cell = g_next_buffer.at(
            static_cast<size_t>(cur_y * g_term_width + start_x + 1 + col_idx));
        cell.glyph.fill(0);
        if (col_idx >= l_pad && col_idx < l_pad + line_len) {
          cell.glyph.at(0) = static_cast<uint8_t>(
              footer_text[static_cast<size_t>(col_idx - l_pad)]);
        } else {
          cell.glyph.at(0) = ' ';
        }
        cell.fg = hint_fg;
        cell.bg = modal_bg;
      }

      auto& rb = g_next_buffer.at(
          static_cast<size_t>(cur_y * g_term_width + start_x + box_w - 1));
      set_glyph(rb, "\xe2\x94\x82");
      rb.fg = border_color;
      rb.bg = modal_bg;
    }
  }

  // 7. Bottom Border: └───┘
  {
    int bot_y = start_y + box_h - 1;
    if (bot_y < g_term_height) {
      auto& bl =
          g_next_buffer.at(static_cast<size_t>(bot_y * g_term_width + start_x));
      set_glyph(bl, "\xe2\x94\x94");
      bl.fg = border_color;
      bl.bg = modal_bg;

      for (int x = 1; x <= box_inner_w; ++x) {
        auto& cell = g_next_buffer.at(
            static_cast<size_t>(bot_y * g_term_width + start_x + x));
        set_glyph(cell, "\xe2\x94\x80");
        cell.fg = border_color;
        cell.bg = modal_bg;
      }

      auto& br = g_next_buffer.at(
          static_cast<size_t>(bot_y * g_term_width + start_x + box_w - 1));
      set_glyph(br, "\xe2\x94\x98");
      br.fg = border_color;
      br.bg = modal_bg;
    }
  }
}

auto tui_video_initialize() -> void {
  tui_shape_detector_initialize();
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
  empty_cell.glyph.fill(0);
  empty_cell.glyph.at(0) = ' ';
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

static auto get_text_fg_color() -> TuiPixel_t {
  const VideoColor_t* pal = video_get_output_palette();
  if (pal != nullptr) {
    switch (g_videotype) {
      case VT_MONO_AMBER:
        return {pal[MONOCHROME_AMBER].r, pal[MONOCHROME_AMBER].g,
                pal[MONOCHROME_AMBER].b};
      case VT_MONO_GREEN:
        return {pal[MONOCHROME_GREEN].r, pal[MONOCHROME_GREEN].g,
                pal[MONOCHROME_GREEN].b};
      case VT_MONO_WHITE:
        return {pal[MONOCHROME_WHITE].r, pal[MONOCHROME_WHITE].g,
                pal[MONOCHROME_WHITE].b};
      case VT_MONO_CUSTOM:
        return {pal[MONOCHROME_CUSTOM].r, pal[MONOCHROME_CUSTOM].g,
                pal[MONOCHROME_CUSTOM].b};
      default:
        break;
    }
  }
  return {255, 255, 255};
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

  cell.glyph.fill(0);
  cell.glyph.at(0) =
      (ascii < 32 || ascii > 126) ? ' ' : static_cast<uint8_t>(ascii);
  TuiPixel_t text_color = get_text_fg_color();
  TuiPixel_t a2_black = {0, 0, 0};
  cell.fg = text_color;
  cell.bg = a2_black;
  if (attr == 2 || (attr == 3 && !flash_on)) {
    cell.fg = a2_black;
    cell.bg = text_color;
  }

  if (r == hw_cursor_y && c == hw_cursor_x) {
    if (flash_on) {
      set_glyph(cell, "\xe2\x96\x92");  // ▒ Checkerboard
      cell.fg = text_color;
      cell.bg = a2_black;
    }
  }
}

static auto render_gfx_cell(const uint32_t* pixels, int pitch, int width,
                            int sample_height, int x, int y, int gfx_w,
                            int gfx_h, TuiState_t& cell) -> void {
  if (g_render_mode == TUI_RENDER_SMART) {
    int x_start = (gfx_w > 0) ? (x * width / gfx_w) : 0;
    int x_end = (gfx_w > 0) ? ((x + 1) * width / gfx_w) : width;
    int y_start = (gfx_h > 0) ? (y * sample_height / gfx_h) : 0;
    int y_end = (gfx_h > 0) ? ((y + 1) * sample_height / gfx_h) : sample_height;
    tui_shape_detect_cell(pixels, pitch, x_start, y_start, x_end, y_end, &cell);
  } else {
    int sx = (gfx_w > 0) ? (x * width / gfx_w) : 0;
    int sy = (gfx_h > 0) ? (y * sample_height / gfx_h) : 0;
    set_glyph(cell, "\xe2\x96\x80");
    const int stride = pitch / static_cast<int>(sizeof(uint32_t));
    uint32_t p = pixels[static_cast<size_t>(sy * stride + sx)];
    cell.fg = {static_cast<uint8_t>(p & 0xFF),
               static_cast<uint8_t>((p >> 8) & 0xFF),
               static_cast<uint8_t>((p >> 16) & 0xFF)};
    cell.bg = {0, 0, 0};
  }
}

#if ENABLE_DEBUGGER
static auto render_debugger_text_screen() -> void {
  const VideoColor_t* pal = video_get_output_palette();

  constexpr int target_w = DEBUG_VIRTUAL_TEXT_WIDTH;
  constexpr int disasm_max_rows = 32;
  constexpr int prompt_row = 39;

  int avail_rows = g_term_height;
  if (avail_rows > min_term_height_status && !g_fullscreen) {
    avail_rows = g_term_height - 1;
  }

  int off_x = (g_term_width - target_w) / 2;
  if (off_x < 0) off_x = 0;

  int console_rows = 4;
  if (avail_rows < 16) {
    console_rows = 2;
  } else if (avail_rows >= 40) {
    console_rows = avail_rows - disasm_max_rows;
  }

  int top_rows = avail_rows - console_rows;
  if (top_rows > disasm_max_rows) {
    top_rows = disasm_max_rows;
  }
  if (top_rows < 0) {
    top_rows = 0;
  }

  for (int ty = 0; ty < avail_rows; ++ty) {
    int src_r = 0;
    if (ty < top_rows) {
      src_r = ty;
    } else if (ty == avail_rows - 1) {
      src_r = prompt_row;
    } else {
      int dist_from_bottom = (avail_rows - 1) - ty;
      src_r = prompt_row - dist_from_bottom;
      if (src_r < disasm_max_rows) {
        src_r = -1;
      }
    }

    for (int c = 0; c < target_w; ++c) {
      int tx = off_x + c;
      if (tx >= g_term_width) break;

      char ch = ' ';
      ColorRef_t fg_raw = WHITE;
      ColorRef_t bg_raw = BLACK;

      if (src_r >= 0 && src_r < DEBUG_VIRTUAL_TEXT_HEIGHT) {
        ch = g_debugger_virtual_text_screen[src_r][c];
        fg_raw = g_debugger_virtual_text_screen_fg[src_r][c];
        bg_raw = g_debugger_virtual_text_screen_bg[src_r][c];
      }

      TuiState_t& cell =
          g_next_buffer.at(static_cast<size_t>(ty * g_term_width + tx));
      cell.glyph.fill(0);
      cell.glyph.at(0) =
          (ch != 0) ? static_cast<uint8_t>(ch) : static_cast<uint8_t>(' ');

      if (pal != nullptr && fg_raw < max_palette_size) {
        cell.fg = {pal[fg_raw].r, pal[fg_raw].g, pal[fg_raw].b};
      } else {
        cell.fg = {static_cast<uint8_t>(fg_raw & 0xFF),
                   static_cast<uint8_t>((fg_raw >> 8) & 0xFF),
                   static_cast<uint8_t>((fg_raw >> 16) & 0xFF)};
      }

      if (pal != nullptr && bg_raw < max_palette_size) {
        cell.bg = {pal[bg_raw].r, pal[bg_raw].g, pal[bg_raw].b};
      } else {
        cell.bg = {static_cast<uint8_t>(bg_raw & 0xFF),
                   static_cast<uint8_t>((bg_raw >> 8) & 0xFF),
                   static_cast<uint8_t>((bg_raw >> 16) & 0xFF)};
      }
    }
  }
}
#endif

auto tui_video_render_frame(const uint32_t* pixels, int width, int height,
                            int pitch) -> void {
  if (g_term_width <= 1 || g_term_height <= 1) {
    return;
  }
  g_frame_count++;
  bool flash_on = (g_frame_count / flash_divisor) % 2 == 0;

  bool is_debug_mode = (g_state.mode == MODE_DEBUG);
  bool is_text_mode = !is_debug_mode && video_get_sw_text();
  bool is_mixed_mode = !is_debug_mode && video_get_sw_mixed();
  bool is_80col = video_get_sw_80col();
  bool is_page2 = video_get_sw_page2();
  bool alt_charset = video_get_sw_alt_charset();
  uint16_t page_offset = is_page2 ? a2_page2_offset : 0x000;

  int hw_cursor_x = *mem_get_main_ptr(a2_cursor_x_addr);
  int hw_cursor_y = *mem_get_main_ptr(a2_cursor_y_addr);

  int a2_w_cols = is_80col ? a2_cols_80 : a2_cols_40;
  int avail_rows = g_term_height;
  bool show_status = (g_term_height > min_term_height_status && !g_fullscreen);
  if (show_status) {
    avail_rows = g_term_height - 1;
  }

  TuiPixel_t bg_letterbox = {10, 10, 10};

  // Reset next buffer with letterbox color
  for (auto& cell : g_next_buffer) {
    cell.glyph.fill(0);
    cell.glyph.at(0) = ' ';
    cell.fg = {40, 40, 40};
    cell.bg = bg_letterbox;
  }

  if (is_debug_mode) {
#if ENABLE_DEBUGGER
    render_debugger_text_screen();
#endif
  } else if (is_text_mode) {
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
        render_gfx_cell(pixels, pitch, width, gfx_sample_height, x, y, gfx_w,
                        gfx_h, cell);
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
        render_gfx_cell(pixels, pitch, width, height, x, y, gfx_w, gfx_h, cell);
      }
    }
  }

  if (g_show_help) {
    render_help_overlay();
  } else if (tui_disk_select_is_active()) {
    render_disk_select_overlay();
  }

  g_output_buffer.clear();
  g_output_buffer.push_back('\x1b');
  g_output_buffer.push_back('[');
  g_output_buffer.push_back('H');
  TuiPixel_t curr_fg = {1, 1, 1}, curr_bg = {1, 1, 1};

  for (int y = 0; y < g_term_height; ++y) {
    if (y == g_term_height - 1 && show_status) {
      std::array<char, 128> status{};
      int slen =
          snprintf(status.data(), status.size(),
                   "\x1b[%d;1H\x1b[0m\x1b[48;5;240m\x1b[38;5;255m "
                   "LinApple-TUI | F1: Help | F3: D1 | F4: D2 | F5: Swap | "
                   "F6: Full | F12: Quit ",
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
        for (size_t i = 0; i < next.glyph.size() && next.glyph.at(i) != 0;
             ++i) {
          g_output_buffer.push_back(static_cast<char>(next.glyph.at(i)));
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
  if (isatty(STDOUT_FILENO) != 0) {
    const ssize_t written =
        write(STDOUT_FILENO, g_output_buffer.data(), g_output_buffer.size());
    (void)written;
  }
}

auto tui_video_save_screenshot() -> void {
  if (g_term_width <= 0 || g_term_height <= 0 || g_next_buffer.empty()) {
    return;
  }

  bool show_status = (g_term_height > min_term_height_status && !g_fullscreen);

  // Find next available sequence number
  struct stat st{};
  static int seq = 1;
  std::array<char, 64> ans_name{};
  std::array<char, 64> txt_name{};

  while (true) {
    snprintf(ans_name.data(), ans_name.size(), "linapple%07d.ans", seq);
    snprintf(txt_name.data(), txt_name.size(), "linapple%07d.txt", seq);
    if (stat(ans_name.data(), &st) != 0 && stat(txt_name.data(), &st) != 0) {
      break;
    }
    seq++;
  }

  // Create copy of full screen buffer
  std::vector<TuiState_t> screen_buf = g_next_buffer;
  if (show_status && g_term_height > 0) {
    int status_y = g_term_height - 1;
    const std::string status_text =
        " LinApple-TUI | F1: Help | F3: D1 | F4: D2 | F5: Swap | F6: Full | "
        "F12: Quit ";
    for (int x = 0; x < g_term_width; ++x) {
      size_t idx = static_cast<size_t>(status_y * g_term_width + x);
      if (idx >= screen_buf.size()) break;
      screen_buf.at(idx).glyph.fill(0);
      screen_buf.at(idx).glyph.at(0) =
          (static_cast<size_t>(x) < status_text.size())
              ? static_cast<uint8_t>(status_text.at(static_cast<size_t>(x)))
              : static_cast<uint8_t>(' ');
      screen_buf.at(idx).fg = {255, 255, 255};
      screen_buf.at(idx).bg = {70, 70, 70};
    }
  }

  // 1. Write ANSI (.ans) file
  FilePtr_t fp_ans{fopen(ans_name.data(), "wb"), fclose};
  if (fp_ans != nullptr) {
    bool ans_ok = true;
    TuiPixel_t curr_fg = {1, 1, 1};
    TuiPixel_t curr_bg = {1, 1, 1};

    for (int y = 0; y < g_term_height; ++y) {
      for (int x = 0; x < g_term_width; ++x) {
        const TuiState_t& cell =
            screen_buf.at(static_cast<size_t>(y * g_term_width + x));

        if (cell.fg != curr_fg) {
          if (fprintf(fp_ans.get(), "\x1b[38;2;%d;%d;%dm", cell.fg.r, cell.fg.g,
                      cell.fg.b) < 0) {
            ans_ok = false;
          }
          curr_fg = cell.fg;
        }
        if (cell.bg != curr_bg) {
          if (fprintf(fp_ans.get(), "\x1b[48;2;%d;%d;%dm", cell.bg.r, cell.bg.g,
                      cell.bg.b) < 0) {
            ans_ok = false;
          }
          curr_bg = cell.bg;
        }

        for (size_t i = 0; i < cell.glyph.size() && cell.glyph.at(i) != 0;
             ++i) {
          if (fputc(static_cast<int>(cell.glyph.at(i)), fp_ans.get()) == EOF) {
            ans_ok = false;
          }
        }
      }
      if (fprintf(fp_ans.get(), "\x1b[0m\n") < 0) {
        ans_ok = false;
      }
      curr_fg = {1, 1, 1};
      curr_bg = {1, 1, 1};
    }
    if (fflush(fp_ans.get()) != 0 || !ans_ok || ferror(fp_ans.get()) != 0) {
      fp_ans.reset();
      unlink(ans_name.data());
    }
  }

  // 2. Write Plain Text (.txt) file
  FilePtr_t fp_txt{fopen(txt_name.data(), "wb"), fclose};
  if (fp_txt != nullptr) {
    bool txt_ok = true;
    for (int y = 0; y < g_term_height; ++y) {
      std::string line_str;
      for (int x = 0; x < g_term_width; ++x) {
        const TuiState_t& cell =
            screen_buf.at(static_cast<size_t>(y * g_term_width + x));
        for (size_t i = 0; i < cell.glyph.size() && cell.glyph.at(i) != 0;
             ++i) {
          line_str.push_back(static_cast<char>(cell.glyph.at(i)));
        }
      }
      while (!line_str.empty() && line_str.back() == ' ') {
        line_str.pop_back();
      }
      line_str.push_back('\n');
      if (fwrite(line_str.data(), 1, line_str.size(), fp_txt.get()) !=
          line_str.size()) {
        txt_ok = false;
      }
    }
    if (fflush(fp_txt.get()) != 0 || !txt_ok || ferror(fp_txt.get()) != 0) {
      fp_txt.reset();
      unlink(txt_name.data());
    }
  }

  seq++;
}
