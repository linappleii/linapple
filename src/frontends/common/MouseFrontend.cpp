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

  bool capture = true;
  config_load_bool(cfg_sec_configuration, cfg_mouse_capture, &capture);
  g_capture_enabled = capture;
}

auto mouse_frontend_card_present() -> bool { return g_card_slot != 0; }

auto mouse_frontend_card_slot() -> int { return g_card_slot; }

auto mouse_frontend_capture_enabled() -> bool { return g_capture_enabled; }

auto mouse_frontend_dispatch_button(uint8_t button, bool is_down) -> void {
  if (g_card_slot == 0) {
    return;
  }
  MouseButtonPayload_t payload{
      button, static_cast<uint8_t>(is_down ? 1 : 0), {0, 0}};
  peripheral_command_by_id(g_card_slot, k_mouse_card_id, mouse_cmd_set_button,
                           &payload, sizeof(payload));
}

auto mouse_frontend_dispatch_motion(int x, int y) -> void {
  (void)x;
  (void)y;
}
