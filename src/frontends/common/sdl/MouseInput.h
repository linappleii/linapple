// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "frontends/common/MouseFrontend.h"

enum class MouseHostButton { left, middle, right, other };

// The host pointer is taken only while something on the Apple side consumes
// it: a mouse card, or a joystick emulated from the mouse. The first left click
// on the uncaptured window captures and is swallowed; "Mouse Capture = 0"
// forbids every capture while still honouring a release.
auto mouse_input_consumer_present() -> bool;
auto mouse_input_is_captured() -> bool;
auto mouse_input_release() -> void;
// True for the one click the caller handles itself: a left click while the
// debugger owns the screen.
auto mouse_input_button_down(MouseHostButton button, bool release_modifier,
                             bool toolbar_key_held) -> bool;
auto mouse_input_button_up(MouseHostButton button) -> void;
// dx and dy the event's relative motion, x and y its position, in pixels.
auto mouse_input_motion(int dx, int dy, int x, int y) -> void;

// Provided by each frontend's Frame.cpp.
auto frame_pointer_capture(bool captured, bool relative) -> void;
auto frame_picture_rect() -> MousePictureRect;
