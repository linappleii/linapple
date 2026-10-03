// SPDX-License-Identifier: GPL-2.0-only

#include "frontends/common/sdl/MouseFrontend.h"

#include <cstddef>
#include <cstdint>

#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/sdl/JoystickFrontend.h"

auto mouse_frontend_dispatch_button(uint8_t button, bool is_down) -> void {
  uint8_t mouse_active = 0;
  size_t qsize = 1;
  peripheral_query(mouse_default_slot, mouse_query_is_active, &mouse_active,
                   &qsize);
  if (mouse_active != 0) {
    MouseButtonPayload_t payload{button, is_down, {0, 0}};
    peripheral_command(mouse_default_slot, mouse_cmd_set_button, &payload,
                       sizeof(payload));
  }
  if (joy_frontend_is_mouse_emulation_active()) {
    joy_frontend_process_mouse_button(static_cast<int>(button), is_down);
  }
}

auto mouse_frontend_dispatch_motion(int x, int y) -> void {
  uint8_t mouse_active = 0;
  size_t qsize = 1;
  peripheral_query(mouse_default_slot, mouse_query_is_active, &mouse_active,
                   &qsize);
  if (mouse_active != 0) {
    MousePosPayload_t payload{x, VIEWPORTCX, y, VIEWPORTCY};
    peripheral_command(mouse_default_slot, mouse_cmd_set_pos, &payload,
                       sizeof(payload));
  }
  if (joy_frontend_is_mouse_emulation_active()) {
    joy_frontend_process_mouse_motion(x, VIEWPORTCX, y, VIEWPORTCY);
  }
}

auto mouse_frontend_should_auto_capture() -> bool {
  bool mouse_capture_cfg = true;
  config_load_bool("Configuration", REGVALUE_MOUSE_CAPTURE, &mouse_capture_cfg);
  if (!mouse_capture_cfg) {
    return false;
  }

  uint8_t mouse_active = 0;
  size_t qsize = 1;
  peripheral_query(mouse_default_slot, mouse_query_is_active, &mouse_active,
                   &qsize);
  const bool mouse_in_use =
      (mouse_active != 0) || joy_frontend_is_mouse_emulation_active();

  return mouse_in_use && (system_state.mode == app_mode_running ||
                          system_state.mode == app_mode_stepping);
}
