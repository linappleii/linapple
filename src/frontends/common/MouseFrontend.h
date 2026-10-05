// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

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

// One xterm SGR mouse report, CSI < Cb ; Cx ; Cy M or m, as any-event
// tracking (?1003) with SGR encoding (?1006) delivers it. Cb's low two bits
// name the button (3 for none), bit 5 marks motion and bit 6 the wheel; under
// any-event tracking every report during a drag carries the held button with
// the motion bit, so such a report is motion with no new edge, not a press.
// Cx and Cy are one-based, in cells or, under ?1016, in pixels.
struct MouseSgrEvent_t {
  int button;
  bool pressed;
  bool released;
  bool motion;
  bool wheel;
  int x;
  int y;
};

// Each decoder takes the whole sequence from its ESC to its final byte and
// answers false for anything that is not exactly the report it names.
auto mouse_frontend_sgr_decode(const uint8_t* seq, size_t len,
                               MouseSgrEvent_t* out) -> bool;
// DECRPM, CSI ? Pd ; Ps $ y: the terminal's answer to a DECRQM query of
// private mode Pd. Ps is 1 or 3 for set, 2 for reset, 4 for permanently
// reset and 0 for a mode the terminal does not know.
auto mouse_frontend_decode_mode_report(const uint8_t* seq, size_t len,
                                       int* mode, int* setting) -> bool;
// XTWINOPS 16's answer, CSI 6 ; height ; width t: one character cell's size
// in pixels.
auto mouse_frontend_decode_cell_size_report(const uint8_t* seq, size_t len,
                                            int* width, int* height) -> bool;
