// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstdint>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "core/LinAppleCore.h"
#include "doctest.h"

auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                     uint32_t cycles) -> uint8_t;

namespace {

constexpr uint16_t ADDR_SPEAKER = 0xC030;

struct ScopedCpuContext_t {
  CpuInstance_t* previous = nullptr;
  CpuInstance_t fresh{};

  ScopedCpuContext_t() : previous(cpu_get_active_context()) {
    cpu_set_active_context(&fresh);
  }

  ~ScopedCpuContext_t() {
    if (previous != nullptr) {
      cpu_set_active_context(previous);
    }
  }

  ScopedCpuContext_t(const ScopedCpuContext_t&) = delete;
  auto operator=(const ScopedCpuContext_t&) -> ScopedCpuContext_t& = delete;
  ScopedCpuContext_t(ScopedCpuContext_t&&) = delete;
  auto operator=(ScopedCpuContext_t&&) -> ScopedCpuContext_t& = delete;
};

}  // namespace

// =============================================================================
// The core seam: the speaker reached through the real direct-I/O bridge
// =============================================================================

TEST_CASE(
    "Speaker Core Seam: Direct IO Bridge Sub-Cycle Cycle Synchronization") {
  // Verifies that when CPU instructions touch $C030 at sub-cycle offsets,
  // the Direct I/O bridge synchronizes CPU cumulative cycles so that
  // host->GetCycles() accurately reflects the instruction cycle rather than
  // stale frame-start cycles.
  ScopedCpuContext_t cpu_scope;
  linapple_init();
  peripheral_manager_init();
  REQUIRE(peripheral_register(speaker_get_descriptor(), 0) == 0);

  const uint64_t initial_cycles = cpu_get_cumulative_cycles();
  constexpr uint32_t instruction_cycle_offset = 512;

  // Simulate an instruction executing at sub-cycle offset 512 within the frame
  io_map_dispatch(0, ADDR_SPEAKER, 0, 0, instruction_cycle_offset);

  // The CPU cumulative cycles MUST advance to include the instruction cycle
  // offset
  CHECK(cpu_get_cumulative_cycles() ==
        initial_cycles + instruction_cycle_offset);

  linapple_shutdown();
}

TEST_CASE(
    "Speaker Core Seam: Intra-Frame 1 kHz Tone Synthesis via Direct IO "
    "Dispatch") {
  // Verifies that multiple speaker strobes distributed across a single 16.6ms
  // video frame (17,030 cycles) produce an active 1 kHz square-wave audio
  // signal rather than collapsing into sample 0 at the frame boundary (which
  // creates low-frequency 60 Hz bumps/mush).
  ScopedCpuContext_t cpu_scope;
  linapple_init();
  peripheral_manager_init();
  REQUIRE(peripheral_register(speaker_get_descriptor(), 0) == 0);

  static std::vector<float> captured_frame_audio;
  captured_frame_audio.clear();

  linapple_set_audio_channel_callback(
      [](const char* peripheral_id, int slot, const float* const* channels,
         size_t num_channels, size_t num_samples) {
        (void)peripheral_id;
        (void)slot;
        if (channels != nullptr && num_channels > 0 && channels[0] != nullptr &&
            num_samples > 0) {
          captured_frame_audio.insert(captured_frame_audio.end(), channels[0],
                                      channels[0] + num_samples);
        }
      });

  // Simulate Apple II ROM BELL1 routine: ~1 kHz square wave (toggling every
  // ~1,000 cycles) across a standard 17,030-cycle frame (~16 strobes across the
  // frame)
  constexpr uint32_t frame_cycles = 17030;
  for (uint32_t cycle = 1000; cycle < 17000; cycle += 1000) {
    io_map_dispatch(0, ADDR_SPEAKER, 0, 0, cycle);
  }

  // Complete CPU execution for the frame
  cpu_calc_cycles(frame_cycles);

  // End of frame: peripheral manager thinks for the whole frame
  peripheral_manager_think(frame_cycles);

  linapple_set_audio_channel_callback(nullptr);

  // One sample per 6502 cycle, so the frame is exactly its own cycle count
  REQUIRE(captured_frame_audio.size() == frame_cycles);

  const auto& mono = captured_frame_audio;

  // Nothing drives the cone before the first strobe at cycle 1000, and
  // previous_input equals the drive level, so those samples are true silence.
  CHECK(mono[999] == doctest::Approx(0.0f));

  // The first edge is a step of exactly 2.0, emitted unclipped: the only
  // conversion to an integer format happens in the mixer.
  constexpr float edge = 2.0f;
  CHECK(mono[1000] == doctest::Approx(edge));

  // Each of the sixteen strobes flips the output's sign, and an exponential
  // decay never crosses zero between them. The first edge leaves silence for
  // positive, which is not a crossing, so fifteen crossings is the whole
  // frame's count. A frame that collapsed into sample zero would give none.
  size_t zero_crossings = 0;
  for (size_t i = 0; i + 1 < mono.size(); ++i) {
    const bool was_negative = mono[i] < 0.0f;
    const bool is_negative = mono[i + 1] < 0.0f;
    zero_crossings += static_cast<size_t>(was_negative != is_negative);
  }
  CHECK(zero_crossings == 15);

  linapple_shutdown();
}
