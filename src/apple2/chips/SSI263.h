// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

typedef struct {
  uint8_t DurationPhoneme;
  uint8_t Inflection;
  uint8_t RateInflection;
  uint8_t CtrlArtAmp;
  uint8_t FilterFreq;
  uint8_t CurrentMode;
} Ssi263A_t;
