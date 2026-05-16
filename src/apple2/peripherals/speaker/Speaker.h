// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Peripheral.h"

constexpr size_t max_speaker_events = 16384;
constexpr size_t speaker_buffer_size = 16384;

enum { sound_none = 0, sound_wave = 1 };

struct SpeakerEvent_t {
  uint64_t cycle = 0;
  bool state = false;
};

// NOLINTBEGIN(readability-identifier-naming)
// Justification: Legacy fields must match the stable .aws save-state format.
struct SS_IO_Speaker {
  uint64_t g_nSpkrLastCycle =
      0;  // Legacy AppleWin field; scheduled for removal
  uint64_t quiet_cycle_count = 0;
  uint32_t recently_active = 0;
  uint32_t state = 0;
  double next_sample_cycle = 0.0;
  uint32_t last_sample_state = 0;
  float filter_state = 0.0f;
};
// NOLINTEND(readability-identifier-naming)

static constexpr int16_t speaker_sample_volume = 0x4000;

auto Speaker_GetDescriptor() -> Peripheral_t*;

/**
 * @brief Sound Synthesis API
 */
auto Speaker_GenerateSamples(void* instance, uint32_t elapsed_cycles) -> void;
auto Speaker_GetEvents(void* instance, SpeakerEvent_t* event_buffer,
                       uint32_t buffer_capacity) -> uint32_t;
auto Speaker_GetLastCycle(void* instance) -> uint64_t;

enum { speaker_query_is_active = 0x100 };
