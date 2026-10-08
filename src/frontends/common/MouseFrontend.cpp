// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/MouseFrontend.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/Registry.h"

namespace {

constexpr const char* mouse_card_id = "linapple.mouse";
constexpr int first_slot = 1;
constexpr int last_slot = 7;

int card_slot = 0;
bool capture_enabled = true;

// The fraction of a count one event leaves over is spent by the next.
int carry_x = 0;
int carry_y = 0;

auto counts_for(int* carry, int delta, int counts_per_picture, int picture)
    -> int {
  if (picture <= 0) {
    return 0;
  }
  *carry += delta * counts_per_picture;
  const int sent = *carry / picture;
  *carry -= sent * picture;
  return sent;
}

// A position outside the picture is held at its edge, so the result never
// leaves the window; with a minimum above the maximum the card pins at the
// minimum, and so does this.
auto follow_axis(int offset, int extent, int low, int high) -> int {
  if (extent <= 1) {
    return low;
  }
  const int span = extent - 1;
  const int at = std::max(0, std::min(span, offset));
  const int64_t scaled = static_cast<int64_t>(at) * (high - low);
  const int target = low + static_cast<int>((scaled + (span / 2)) / span);
  return std::max(low, std::min(high, target));
}

constexpr uint8_t escape_byte = 0x1B;
constexpr size_t csi_max_params = 3;
// No coordinate or mode number needs more digits; a longer run is noise.
constexpr int csi_max_param_digits = 6;

struct CsiReport {
  uint8_t marker;
  std::array<int, csi_max_params> params;
  size_t param_count;
  uint8_t intermediate;
  uint8_t final;
};

auto parse_csi(const uint8_t* seq, size_t len, CsiReport* out) -> bool {
  if (seq == nullptr || out == nullptr || len < 3 || seq[0] != escape_byte ||
      seq[1] != '[') {
    return false;
  }
  *out = CsiReport{};
  size_t i = 2;
  if (seq[i] == '<' || seq[i] == '?') {
    out->marker = seq[i];
    ++i;
  }
  int digits = 0;
  bool in_param = false;
  while (i < len && ((seq[i] >= '0' && seq[i] <= '9') || seq[i] == ';')) {
    if (seq[i] == ';') {
      if (!in_param || out->param_count >= csi_max_params) {
        return false;
      }
      ++out->param_count;
      digits = 0;
      in_param = false;
    } else {
      if (!in_param) {
        if (out->param_count >= csi_max_params) {
          return false;
        }
        in_param = true;
      }
      if (++digits > csi_max_param_digits) {
        return false;
      }
      int& param = out->params.at(out->param_count);
      param = (param * 10) + (seq[i] - '0');
    }
    ++i;
  }
  if (in_param) {
    ++out->param_count;
  }
  if (i < len && seq[i] >= 0x20 && seq[i] <= 0x2F) {
    out->intermediate = seq[i];
    ++i;
  }
  if (i + 1 != len || seq[i] < 0x40 || seq[i] > 0x7E) {
    return false;
  }
  out->final = seq[i];
  return true;
}

}  // namespace

auto mouse_frontend_initialize() -> void {
  // With two cards the lower slot gets the host.
  card_slot = 0;
  for (int slot = first_slot; slot <= last_slot; ++slot) {
    uint8_t active = 0;
    size_t size = sizeof(active);
    if (peripheral_query_by_id(slot, mouse_card_id, mouse_query_is_active,
                               &active, &size) == peripheral_ok) {
      card_slot = slot;
      break;
    }
  }
  carry_x = 0;
  carry_y = 0;

  bool capture = true;
  config_load_bool(cfg_sec_configuration, cfg_mouse_capture, &capture);
  capture_enabled = capture;
}

auto mouse_frontend_card_present() -> bool { return card_slot != 0; }

auto mouse_frontend_card_slot() -> int { return card_slot; }

auto mouse_frontend_capture_enabled() -> bool { return capture_enabled; }

auto mouse_frontend_button(bool down) -> void {
  if (card_slot == 0) {
    return;
  }
  MouseButtonPayload_t payload{0, static_cast<uint8_t>(down ? 1 : 0), {0, 0}};
  peripheral_command_by_id(card_slot, mouse_card_id, mouse_cmd_set_button,
                           &payload, sizeof(payload));
}

auto mouse_frontend_motion(int dx, int dy, int picture_w, int picture_h)
    -> void {
  if (card_slot == 0) {
    return;
  }
  MouseMovePayload_t payload{
      counts_for(&carry_x, dx, mouse_counts_across, picture_w),
      counts_for(&carry_y, dy, mouse_counts_down, picture_h),
  };
  if (payload.dx == 0 && payload.dy == 0) {
    return;
  }
  peripheral_command_by_id(card_slot, mouse_card_id, mouse_cmd_move, &payload,
                           sizeof(payload));
}

auto mouse_frontend_follow(int host_x, int host_y, MousePictureRect picture)
    -> void {
  if (card_slot == 0) {
    return;
  }
  MousePositionReport_t card{};
  size_t size = sizeof(card);
  if (peripheral_query_by_id(card_slot, mouse_card_id, mouse_query_position,
                             &card, &size) != peripheral_ok ||
      card.tracking == 0) {
    return;
  }
  const MouseMovePayload_t payload{
      follow_axis(host_x - picture.x, picture.w, card.min_x, card.max_x) -
          card.x,
      follow_axis(host_y - picture.y, picture.h, card.min_y, card.max_y) -
          card.y,
  };
  if (payload.dx == 0 && payload.dy == 0) {
    return;
  }
  peripheral_command_by_id(card_slot, mouse_card_id, mouse_cmd_move, &payload,
                           sizeof(payload));
}

auto mouse_frontend_sgr_decode(const uint8_t* seq, size_t len,
                               MouseSgrEvent* out) -> bool {
  CsiReport report{};
  if (out == nullptr || !parse_csi(seq, len, &report) || report.marker != '<' ||
      report.param_count != csi_max_params || report.intermediate != 0 ||
      (report.final != 'M' && report.final != 'm')) {
    return false;
  }
  const int cb = report.params.at(0);
  out->button = cb & 3;
  out->motion = (cb & 32) != 0;
  out->wheel = (cb & 64) != 0;
  out->released = report.final == 'm';
  out->pressed =
      report.final == 'M' && !out->motion && !out->wheel && out->button != 3;
  out->x = report.params.at(1);
  out->y = report.params.at(2);
  return true;
}

auto mouse_frontend_decode_mode_report(const uint8_t* seq, size_t len,
                                       int* mode, int* setting) -> bool {
  CsiReport report{};
  if (mode == nullptr || setting == nullptr || !parse_csi(seq, len, &report) ||
      report.marker != '?' || report.param_count != 2 ||
      report.intermediate != '$' || report.final != 'y') {
    return false;
  }
  *mode = report.params.at(0);
  *setting = report.params.at(1);
  return true;
}

auto mouse_frontend_decode_cell_size_report(const uint8_t* seq, size_t len,
                                            int* width, int* height) -> bool {
  CsiReport report{};
  if (width == nullptr || height == nullptr || !parse_csi(seq, len, &report) ||
      report.marker != 0 || report.param_count != csi_max_params ||
      report.params.at(0) != 6 || report.intermediate != 0 ||
      report.final != 't') {
    return false;
  }
  *height = report.params.at(1);
  *width = report.params.at(2);
  return true;
}
