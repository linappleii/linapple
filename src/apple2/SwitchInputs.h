// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

// The three pushbutton inputs PB0-PB2 the 74LS251 behind $C061-$C063 puts on
// D7. Each line is a wire two devices can pull to +5 V: the game connector's
// button and, on the //e, the keyboard's Open Apple (PB0), Solid Apple (PB1)
// or, with the shift-key mod, the shift key (PB2); nothing latches it, so a
// read sees the switches as they stand (Apple II Reference Manual 1979
// pp. 99-100; IIe Technical Reference pp. 13, 41, Figure 7-14b). The resting
// level with every switch open is set by whatever pull-down hangs on the
// line: the //e keyboard's 470 ohm on PB0 and PB1, a controller plug's 560
// ohm on the lines its buttons drive, else the open TTL input reads high
// (Sather, Understanding the Apple IIe, 7-8; Apple IIe Technical Note #9).
enum SwitchInputSource_t : uint8_t {
  switch_source_keyboard = 0,
  switch_source_connector = 1
};

constexpr uint8_t switch_input_count = 3;

// A new machine: every switch open, no plug, the jumper out, the pull-downs
// as the hardware's. The //e keyboard brings its own pull-downs; a II or II
// Plus board has none and a keyboard-less //e reads PB0 and PB1 high through
// the revision C board's 12 k pull-ups (820-0087-C and later; Technical Note
// #9), which is what makes its reset routine enter the self-test.
auto switch_inputs_reset_configuration(bool apple2e, bool keyboard_present)
    -> void;

auto switch_inputs_set_level(uint8_t line, SwitchInputSource_t source,
                             bool closed) -> void;
// The lines a controller plug's resistors pull down, one bit per line.
auto switch_inputs_set_connector_pulldowns(uint8_t mask) -> void;
// X6 on the //e board: PB2 then follows the shift key, inverted, and a
// pushbutton on PB2 is out of reach (IIe Technical Reference p. 41; Sather
// IIe 7-31).
auto switch_inputs_set_shift_key_mod(bool jumper_in) -> void;
// A mask of 0-7 replaces the hardware's pull-downs; -1 restores them. The
// 820-0064-B and 820-0087-A boards have no pull-ups and leave a disconnected
// keyboard's lines undefined, which only an override can model.
auto switch_inputs_override_pulldowns(int mask) -> void;

auto switch_inputs_pulldowns() -> uint8_t;
auto switch_inputs_level(uint8_t line) -> bool;
