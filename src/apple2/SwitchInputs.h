// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

// PB0-PB2, the 74LS251 inputs behind $C061-$C063. Each is a wire the game
// connector's button and, on the //e, an Apple key or the shift-key mod can
// pull to +5 V; nothing latches it (Apple II Reference Manual 1979 pp. 99-100;
// IIe Technical Reference pp. 13, 41). Open, a line rests at whatever
// pull-down hangs on it, else its TTL input reads high (Sather, Understanding
// the Apple IIe, 7-8; Apple IIe Technical Note #9).
enum SwitchInputSource : uint8_t {
  switch_source_keyboard = 0,
  switch_source_connector = 1,
};
using SwitchInputSource = SwitchInputSource;

constexpr uint8_t switch_input_count = 3;

// A keyboard-less //e reads PB0 and PB1 high through the revision C board's
// 12 k pull-ups (820-0087-C and later; Apple IIe Technical Note #9), which is
// what sends its reset routine into the self-test.
auto switch_inputs_reset_configuration(bool apple2e, bool keyboard_present)
    -> void;

auto switch_inputs_set_level(uint8_t line, SwitchInputSource source,
                             bool closed) -> void;
auto switch_inputs_set_connector_pulldowns(uint8_t mask) -> void;
// X6 on the //e board: PB2 then follows the shift key, inverted, and a button
// on PB2 is out of reach (IIe Technical Reference p. 41; Sather IIe 7-31).
auto switch_inputs_set_shift_key_mod(bool jumper_in) -> void;
// -1 restores the hardware's. The 820-0064-B and 820-0087-A boards have no
// pull-ups and leave an unplugged keyboard's lines undefined, which only an
// override can model.
auto switch_inputs_override_pulldowns(int mask) -> void;

auto switch_inputs_pulldowns() -> uint8_t;
auto switch_inputs_level(uint8_t line) -> bool;
