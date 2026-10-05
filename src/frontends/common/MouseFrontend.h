// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

constexpr uint8_t k_mouse_button_left = 0;
constexpr uint8_t k_mouse_button_right = 1;

// Finds the mouse card in the machine and reads the capture setting. Run once
// the cards exist, and again after anything that can change them: a
// configuration restart or a save-state load.
auto mouse_frontend_initialize() -> void;
auto mouse_frontend_card_present() -> bool;
// 0 when no slot holds the card.
auto mouse_frontend_card_slot() -> int;
auto mouse_frontend_capture_enabled() -> bool;
auto mouse_frontend_dispatch_button(uint8_t button, bool is_down) -> void;
auto mouse_frontend_dispatch_motion(int x, int y) -> void;
