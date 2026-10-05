// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/MouseFrontend.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/Registry.h"

namespace {

constexpr const char* k_mouse_card_id = "linapple.mouse";
constexpr int k_first_slot = 1;
constexpr int k_last_slot = 7;

int g_card_slot = 0;
bool g_capture_enabled = true;

// Host motion times the count scale, less what has already been sent, so
// the fraction of a count left over by one event is spent by the next.
int g_carry_x = 0;
int g_carry_y = 0;

// Integer division truncates toward zero and the remainder keeps the
// dividend's sign, so a carry never pushes the position across zero on its
// own.
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

constexpr uint8_t k_escape = 0x1B;
constexpr size_t k_csi_max_params = 3;
// A terminal coordinate or mode number never needs more digits; a longer
// run is noise, not a report.
constexpr int k_csi_max_param_digits = 6;

// CSI, an optional private marker, up to three decimal parameters, an
// optional intermediate byte and the final byte.
struct CsiReport_t {
  uint8_t marker;
  std::array<int, k_csi_max_params> params;
  size_t param_count;
  uint8_t intermediate;
  uint8_t final;
};

auto parse_csi(const uint8_t* seq, size_t len, CsiReport_t* out) -> bool {
  if (seq == nullptr || out == nullptr || len < 3 || seq[0] != k_escape ||
      seq[1] != '[') {
    return false;
  }
  *out = CsiReport_t{};
  size_t i = 2;
  if (seq[i] == '<' || seq[i] == '?') {
    out->marker = seq[i];
    ++i;
  }
  int digits = 0;
  bool in_param = false;
  while (i < len && ((seq[i] >= '0' && seq[i] <= '9') || seq[i] == ';')) {
    if (seq[i] == ';') {
      if (!in_param || out->param_count >= k_csi_max_params) {
        return false;
      }
      ++out->param_count;
      digits = 0;
      in_param = false;
    } else {
      if (!in_param) {
        if (out->param_count >= k_csi_max_params) {
          return false;
        }
        in_param = true;
      }
      if (++digits > k_csi_max_param_digits) {
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
  g_card_slot = 0;
  for (int slot = k_first_slot; slot <= k_last_slot; ++slot) {
    uint8_t active = 0;
    size_t size = sizeof(active);
    if (peripheral_query_by_id(slot, k_mouse_card_id, mouse_query_is_active,
                               &active, &size) == peripheral_ok) {
      g_card_slot = slot;
      break;
    }
  }
  g_carry_x = 0;
  g_carry_y = 0;

  bool capture = true;
  config_load_bool(cfg_sec_configuration, cfg_mouse_capture, &capture);
  g_capture_enabled = capture;
}

auto mouse_frontend_card_present() -> bool { return g_card_slot != 0; }

auto mouse_frontend_card_slot() -> int { return g_card_slot; }

auto mouse_frontend_capture_enabled() -> bool { return g_capture_enabled; }

auto mouse_frontend_button(bool down) -> void {
  if (g_card_slot == 0) {
    return;
  }
  MouseButtonPayload_t payload{0, static_cast<uint8_t>(down ? 1 : 0), {0, 0}};
  peripheral_command_by_id(g_card_slot, k_mouse_card_id, mouse_cmd_set_button,
                           &payload, sizeof(payload));
}

auto mouse_frontend_motion(int dx, int dy, int picture_w, int picture_h)
    -> void {
  if (g_card_slot == 0) {
    return;
  }
  MouseMovePayload_t payload{
      counts_for(&g_carry_x, dx, k_mouse_counts_across, picture_w),
      counts_for(&g_carry_y, dy, k_mouse_counts_down, picture_h)};
  if (payload.dx == 0 && payload.dy == 0) {
    return;
  }
  peripheral_command_by_id(g_card_slot, k_mouse_card_id, mouse_cmd_move,
                           &payload, sizeof(payload));
}

auto mouse_frontend_sgr_decode(const uint8_t* seq, size_t len,
                               MouseSgrEvent_t* out) -> bool {
  CsiReport_t report{};
  if (out == nullptr || !parse_csi(seq, len, &report) || report.marker != '<' ||
      report.param_count != k_csi_max_params || report.intermediate != 0 ||
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
  CsiReport_t report{};
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
  CsiReport_t report{};
  if (width == nullptr || height == nullptr || !parse_csi(seq, len, &report) ||
      report.marker != 0 || report.param_count != k_csi_max_params ||
      report.params.at(0) != 6 || report.intermediate != 0 ||
      report.final != 't') {
    return false;
  }
  *height = report.params.at(1);
  *width = report.params.at(2);
  return true;
}
