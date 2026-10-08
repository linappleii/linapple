// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/SwitchInputs.h"

#include <array>
#include <cstdint>

namespace {

constexpr uint8_t pulldown_mask_all = 0x07;
constexpr uint8_t keyboard_pulldowns_2e = 0x03;
constexpr uint8_t shift_mod_line = 2;
constexpr int override_none = -1;

struct SwitchInputs {
  std::array<bool, switch_input_count> keyboard_closed{};
  std::array<bool, switch_input_count> connector_closed{};
  uint8_t plug_pulldowns = 0;
  bool shift_key_mod = false;
  bool apple2e = true;
  bool keyboard_present = true;
  int override_pulldowns = override_none;
};

SwitchInputs switch_inputs_state;

}  // namespace

auto switch_inputs_reset_configuration(bool apple2e, bool keyboard_present)
    -> void {
  switch_inputs_state = SwitchInputs{};
  switch_inputs_state.apple2e = apple2e;
  switch_inputs_state.keyboard_present = keyboard_present;
}

auto switch_inputs_set_level(uint8_t line, SwitchInputSource source,
                             bool closed) -> void {
  if (line >= switch_input_count) {
    return;
  }
  if (source == switch_source_keyboard) {
    switch_inputs_state.keyboard_closed.at(line) = closed;
  } else {
    switch_inputs_state.connector_closed.at(line) = closed;
  }
}

auto switch_inputs_set_connector_pulldowns(uint8_t mask) -> void {
  switch_inputs_state.plug_pulldowns = mask & pulldown_mask_all;
}

auto switch_inputs_set_shift_key_mod(bool jumper_in) -> void {
  switch_inputs_state.shift_key_mod = jumper_in;
}

auto switch_inputs_override_pulldowns(int mask) -> void {
  switch_inputs_state.override_pulldowns =
      mask < 0 ? override_none : (mask & pulldown_mask_all);
}

// The 470 ohm pull-downs on PB0 and PB1 are on the //e keyboard, not the board
// (Apple IIe Technical Note #9; Sather IIe 7-8, Figure 7.4), so they go with
// it; a II or II Plus keyboard carries none.
auto switch_inputs_pulldowns() -> uint8_t {
  if (switch_inputs_state.override_pulldowns != override_none) {
    return static_cast<uint8_t>(switch_inputs_state.override_pulldowns);
  }
  uint8_t mask = switch_inputs_state.plug_pulldowns;
  if (switch_inputs_state.apple2e && switch_inputs_state.keyboard_present) {
    mask |= keyboard_pulldowns_2e;
  }
  return mask;
}

// The shift-key mod grounds PB2 while shift is down and holds it at 1 k to
// +5 V otherwise, so with the jumper in a connector button on PB2 changes
// nothing (IIe Technical Reference p. 41; Sather IIe 7-31).
auto switch_inputs_level(uint8_t line) -> bool {
  if (line >= switch_input_count) {
    return false;
  }
  if (line == shift_mod_line) {
    if (switch_inputs_state.shift_key_mod) {
      return !switch_inputs_state.keyboard_closed.at(line);
    }
    if (switch_inputs_state.connector_closed.at(line)) {
      return true;
    }
  } else if (switch_inputs_state.keyboard_closed.at(line) ||
             switch_inputs_state.connector_closed.at(line)) {
    return true;
  }
  return (switch_inputs_pulldowns() & (1U << line)) == 0;
}
