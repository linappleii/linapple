// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

struct Ssi263A_t {
  uint8_t duration_phoneme;
  uint8_t inflection;
  uint8_t rate_inflection;
  uint8_t ctrl_art_amp;
  uint8_t filter_freq;
  uint8_t current_mode;
};
