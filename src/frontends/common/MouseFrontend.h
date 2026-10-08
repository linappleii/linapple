// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

// The Apple picture as last drawn, in the host's units: window pixels, or the
// terminal's cells or pixels.
struct MousePictureRect {
  int x;
  int y;
  int w;
  int h;
};

// One count per hires pixel of the displayed picture, a frontend policy: a
// program clamped to 0..279 tracks the host pointer one for one. The real mouse
// makes about 50 counts an inch (AppleMouse II User's Manual p. 45).
constexpr int mouse_counts_across = 280;
constexpr int mouse_counts_down = 192;

// Run once the cards exist, and again after a restart or a save-state load.
auto mouse_frontend_initialize() -> void;
auto mouse_frontend_card_present() -> bool;
// 0 when no slot holds the card.
auto mouse_frontend_card_slot() -> int;
auto mouse_frontend_capture_enabled() -> bool;
// The card has one button; the host's left button is it.
auto mouse_frontend_button(bool down) -> void;
// dx and dy in the units of picture_w and picture_h; the fraction of a count
// left over carries to the next call, sign and all.
auto mouse_frontend_motion(int dx, int dy, int picture_w, int picture_h)
    -> void;
// Puts the card's pointer under the host's: the host position within the
// picture maps proportionally onto the card's clamp window, the picture's first
// cell or pixel on the window's low edge and its last on the high edge, and the
// difference from the card's own counters goes out as motion. Nothing is sent
// while SETMOUSE has motion off. For a host that cannot hide its pointer, where
// relative motion would leave the two pointers visibly apart.
auto mouse_frontend_follow(int host_x, int host_y, MousePictureRect picture)
    -> void;

// One xterm SGR mouse report, CSI < Cb ; Cx ; Cy M or m, under any-event
// tracking (?1003) with SGR encoding (?1006). Cb's low two bits name the button
// (3 for none), bit 5 marks motion, bit 6 the wheel; a drag's reports carry the
// held button with the motion bit, so they are motion, not a press. Cx and Cy
// are one-based cells or, under ?1016, pixels.
struct MouseSgrEvent {
  int button;
  bool pressed;
  bool released;
  bool motion;
  bool wheel;
  int x;
  int y;
};

// Each decoder takes the whole sequence, ESC to final byte, and answers false
// for anything but exactly the report it names.
auto mouse_frontend_sgr_decode(const uint8_t* seq, size_t len,
                               MouseSgrEvent* out) -> bool;
// DECRPM, CSI ? Pd ; Ps $ y, the answer to a DECRQM query of private mode Pd:
// Ps 1 or 3 set, 2 reset, 4 permanently reset, 0 unknown mode.
auto mouse_frontend_decode_mode_report(const uint8_t* seq, size_t len,
                                       int* mode, int* setting) -> bool;
// XTWINOPS 16's answer, CSI 6 ; height ; width t: one cell's size in pixels.
auto mouse_frontend_decode_cell_size_report(const uint8_t* seq, size_t len,
                                            int* width, int* height) -> bool;
