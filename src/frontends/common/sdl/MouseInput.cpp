// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/sdl/MouseInput.h"

#include <cstdint>

#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "frontends/common/MouseFrontend.h"
#include "frontends/common/sdl/JoystickFrontend.h"

auto mouse_input_dispatch_button(uint8_t button, bool is_down) -> void {
  mouse_frontend_dispatch_button(button, is_down);
  if (joy_frontend_is_mouse_emulation_active()) {
    joy_frontend_process_mouse_button(static_cast<int>(button), is_down);
  }
}

auto mouse_input_dispatch_motion(int x, int y) -> void {
  mouse_frontend_dispatch_motion(x, y);
  if (joy_frontend_is_mouse_emulation_active()) {
    joy_frontend_process_mouse_motion(x, VIEWPORTCX, y, VIEWPORTCY);
  }
}

auto mouse_input_should_auto_capture() -> bool {
  if (!mouse_frontend_capture_enabled()) {
    return false;
  }
  // Nothing on the Apple side to feed means nothing to take the pointer for.
  const bool consumer_present =
      mouse_frontend_card_present() || joy_frontend_is_mouse_emulation_active();
  return consumer_present && (system_state.mode == app_mode_running ||
                              system_state.mode == app_mode_stepping);
}
