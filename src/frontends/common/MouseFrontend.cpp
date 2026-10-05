// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/MouseFrontend.h"

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
