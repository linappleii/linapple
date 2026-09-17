// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

struct Peripheral_t;

// NOLINTBEGIN(readability-identifier-naming)
// Justification: Legacy fields must match the stable .aws save-state format.
struct SsIoSpeaker_t {
  uint64_t g_spkr_last_cycle =
      0;  // Legacy AppleWin field; scheduled for removal
  uint64_t quiet_cycle_count = 0;
  uint32_t recently_active = 0;
  uint32_t state = 0;
  double next_sample_cycle = 0.0;
  uint32_t last_sample_state = 0;
  float filter_state = 0.0f;
};
// NOLINTEND(readability-identifier-naming)

auto speaker_get_descriptor() -> Peripheral_t*;
