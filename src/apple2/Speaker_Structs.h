#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

static constexpr size_t MAX_SPKR_EVENTS = 4096;
static constexpr size_t SPKR_BUFFER_SIZE = 8192;

struct SpkrEvent {
  uint64_t cycle = 0;
  bool state = false;
};

struct SS_IO_Speaker {
  uint64_t g_nSpkrLastCycle = 0;
};

struct Speaker_t {
  std::array<SpkrEvent, MAX_SPKR_EVENTS> events{};
  int num_events = 0;
  bool state = false;
  uint64_t last_cycle = 0;
  uint64_t quiet_cycle_count = 0;
  bool recently_active = false;
  bool toggle_flag = false;
  uint32_t sound_type = 1;  // Default to SOUND_WAVE (1)

  // Sample generation state
  bool last_sample_state = false;
  double next_sample_cycle = 0.0;
  std::array<int16_t, SPKR_BUFFER_SIZE> sample_buffer{};

  void* host = nullptr;  // Opaque pointer to HostInterface_t
  int slot = 0;
};
