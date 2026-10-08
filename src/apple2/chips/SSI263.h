// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <type_traits>

// Silicon Systems SSI 263A Speech Synthesizer register state.
struct Ssi263A {
  uint8_t duration_phoneme = 0;
  uint8_t inflection = 0;
  uint8_t rate_inflection = 0;
  uint8_t ctrl_art_amp = 0;
  uint8_t filter_freq = 0;
  uint8_t current_mode = 0;
};

static_assert(std::is_standard_layout<Ssi263A>::value,
              "Ssi263A must satisfy standard layout");
static_assert(sizeof(Ssi263A) == 6,
              "Ssi263A must be exactly 6 bytes without padding");
