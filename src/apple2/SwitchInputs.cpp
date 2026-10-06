// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/SwitchInputs.h"

#include <array>
#include <cstdint>

namespace {

constexpr uint8_t pulldown_mask_all = 0x07;
constexpr uint8_t keyboard_pulldowns_2e = 0x03;
constexpr uint8_t shift_mod_line = 2;
constexpr int override_none = -1;

struct SwitchInputs_t {
  std::array<bool, switch_input_count> keyboard_closed{};
  std::array<bool, switch_input_count> connector_closed{};
  uint8_t plug_pulldowns = 0;
  bool shift_key_mod = false;
  bool apple2e = true;
  bool keyboard_present = true;
  int override_pulldowns = override_none;
};

SwitchInputs_t g_switch_inputs;

}  // namespace

auto switch_inputs_reset_configuration(bool apple2e, bool keyboard_present)
    -> void {
  g_switch_inputs = SwitchInputs_t{};
  g_switch_inputs.apple2e = apple2e;
  g_switch_inputs.keyboard_present = keyboard_present;
}

auto switch_inputs_set_level(uint8_t line, SwitchInputSource_t source,
                             bool closed) -> void {
  if (line >= switch_input_count) {
    return;
  }
  if (source == switch_source_keyboard) {
    g_switch_inputs.keyboard_closed.at(line) = closed;
  } else {
    g_switch_inputs.connector_closed.at(line) = closed;
  }
}

auto switch_inputs_set_connector_pulldowns(uint8_t mask) -> void {
  g_switch_inputs.plug_pulldowns = mask & pulldown_mask_all;
}

auto switch_inputs_set_shift_key_mod(bool jumper_in) -> void {
  g_switch_inputs.shift_key_mod = jumper_in;
}

auto switch_inputs_override_pulldowns(int mask) -> void {
  g_switch_inputs.override_pulldowns =
      mask < 0 ? override_none : (mask & pulldown_mask_all);
}

// The //e keyboard's 470 ohm resistors on PB0 and PB1 (Technical Note #9:
// "the low level is ensured by the 470-ohm keyboard pulldown resistor alone";
// Sather IIe 7-8, Figure 7.4) are on the keyboard, not the board, so they go
// with it. A II or II Plus keyboard carries none. The plug's resistors add to
// whatever the keyboard brings.
auto switch_inputs_pulldowns() -> uint8_t {
  if (g_switch_inputs.override_pulldowns != override_none) {
    return static_cast<uint8_t>(g_switch_inputs.override_pulldowns);
  }
  uint8_t mask = g_switch_inputs.plug_pulldowns;
  if (g_switch_inputs.apple2e && g_switch_inputs.keyboard_present) {
    mask |= keyboard_pulldowns_2e;
  }
  return mask;
}

// A closed switch puts +5 V on the line, whichever device closes it. The
// shift key reaches PB2 only through the mod, which grounds the line while
// shift is down and leaves it at 1 k to +5 V otherwise, so with the jumper in
// a connector button on PB2 changes nothing (IIe Technical Reference p. 41;
// Sather IIe 7-31).
auto switch_inputs_level(uint8_t line) -> bool {
  if (line >= switch_input_count) {
    return false;
  }
  if (line == shift_mod_line) {
    if (g_switch_inputs.shift_key_mod) {
      return !g_switch_inputs.keyboard_closed.at(line);
    }
    if (g_switch_inputs.connector_closed.at(line)) {
      return true;
    }
  } else if (g_switch_inputs.keyboard_closed.at(line) ||
             g_switch_inputs.connector_closed.at(line)) {
    return true;
  }
  return (switch_inputs_pulldowns() & (1U << line)) == 0;
}
