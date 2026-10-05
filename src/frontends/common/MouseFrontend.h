// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// The Apple picture as the frontend last drew it, in the host's own units:
// window pixels, or the terminal's cells or pixels.
struct MousePictureRect_t {
  int x;
  int y;
  int w;
  int h;
};

// One count per hires pixel of the displayed picture, at any zoom and in any
// letterbox: a frontend policy, not a hardware fact. A program clamped to
// 0..279 then tracks the host pointer one for one across the picture; the
// real AppleMouse makes about 50 counts an inch (AppleMouse II User's Manual
// p. 45), so a 0..1023 program needs several picture widths of travel.
constexpr int k_mouse_counts_across = 280;
constexpr int k_mouse_counts_down = 192;

// Finds the mouse card in the machine and reads the capture setting. Run once
// the cards exist, and again after anything that can change them: a
// configuration restart or a save-state load.
auto mouse_frontend_initialize() -> void;
auto mouse_frontend_card_present() -> bool;
// 0 when no slot holds the card.
auto mouse_frontend_card_slot() -> int;
auto mouse_frontend_capture_enabled() -> bool;
// The card has one button; the host's left button is it.
auto mouse_frontend_button(bool down) -> void;
// dx and dy in the units picture_w and picture_h are measured in. Whatever
// falls short of a whole count carries to the next call, sign and all.
auto mouse_frontend_motion(int dx, int dy, int picture_w, int picture_h)
    -> void;
