// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "frontends/common/MouseFrontend.h"

// What one host mouse event becomes for the Apple side: the card's command
// and, when a joystick is emulated from the mouse, that path's input too.
auto mouse_input_dispatch_button(uint8_t button, bool is_down) -> void;
auto mouse_input_dispatch_motion(int x, int y) -> void;
// Whether a click on the uncaptured window takes the pointer.
auto mouse_input_should_auto_capture() -> bool;
