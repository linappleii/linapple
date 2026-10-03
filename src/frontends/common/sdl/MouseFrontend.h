// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

constexpr uint8_t k_mouse_button_left = 0;
constexpr uint8_t k_mouse_button_right = 1;

auto mouse_frontend_dispatch_button(uint8_t button, bool is_down) -> void;
auto mouse_frontend_dispatch_motion(int x, int y) -> void;
auto mouse_frontend_should_auto_capture() -> bool;
