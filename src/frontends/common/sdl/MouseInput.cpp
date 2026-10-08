// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/sdl/MouseInput.h"

#include "core/LinAppleCore.h"
#include "frontends/common/MouseFrontend.h"
#include "frontends/common/sdl/JoystickFrontend.h"

namespace {

bool mouse_captured = false;

auto capture_allowed() -> bool {
  return mouse_frontend_capture_enabled() && mouse_input_consumer_present();
}

// Relative mode is for the card alone: in it SDL's x and y become a virtual
// position, which the joystick-as-mouse path reads in place of the pointer. The
// capture state never waits on SDL's result, which the dummy driver may refuse.
auto set_captured(bool captured) -> void {
  mouse_captured = captured;
  frame_pointer_capture(captured, captured && mouse_frontend_card_present());
}

auto machine_is_live() -> bool {
  return system_state.mode == app_mode_running ||
         system_state.mode == app_mode_stepping;
}

}  // namespace

auto mouse_input_consumer_present() -> bool {
  return mouse_frontend_card_present() ||
         joy_frontend_is_mouse_emulation_active();
}

auto mouse_input_is_captured() -> bool { return mouse_captured; }

auto mouse_input_release() -> void { set_captured(false); }

auto mouse_input_button_down(MouseHostButton button, bool release_modifier,
                             bool toolbar_key_held) -> bool {
  if (button == MouseHostButton::middle) {
    set_captured(!mouse_captured && capture_allowed());
    return false;
  }
  if (button != MouseHostButton::left || toolbar_key_held) {
    return false;
  }
  if (system_state.mode == app_mode_debug) {
    return true;
  }
  if (!mouse_captured) {
    if (capture_allowed() && machine_is_live()) {
      set_captured(true);
    }
    return false;
  }
  if (release_modifier) {
    set_captured(false);
    return false;
  }
  mouse_frontend_button(true);
  if (joy_frontend_is_mouse_emulation_active()) {
    joy_frontend_process_mouse_button(0, true);
  }
  return false;
}

auto mouse_input_button_up(MouseHostButton button) -> void {
  if (!mouse_captured || button != MouseHostButton::left) {
    return;
  }
  mouse_frontend_button(false);
  if (joy_frontend_is_mouse_emulation_active()) {
    joy_frontend_process_mouse_button(0, false);
  }
}

auto mouse_input_motion(int dx, int dy, int x, int y) -> void {
  if (!mouse_captured) {
    return;
  }
  const MousePictureRect picture = frame_picture_rect();
  mouse_frontend_motion(dx, dy, picture.w, picture.h);
  if (joy_frontend_is_mouse_emulation_active()) {
    joy_frontend_process_mouse_motion(x - picture.x, picture.w, y - picture.y,
                                      picture.h);
  }
}
