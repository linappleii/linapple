// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <vector>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/FramePacer.h"

namespace {

constexpr uint32_t NTSC_FRAME_CYCLES = 17030;
constexpr uint32_t PAL_FRAME_CYCLES = 20313;

// A frame of wall time is the machine.s frame in cycles over its clock. The
// pacer derives it the same way; stated here so a formula change fails.
auto period_ns_for(uint32_t cycles, double clock_hz) -> int64_t {
  return static_cast<int64_t>((static_cast<double>(cycles) * 1e9) / clock_hz);
}

const int64_t NTSC_PERIOD_NS =
    period_ns_for(NTSC_FRAME_CYCLES, CLOCK_6502_NTSC);
const int64_t PAL_PERIOD_NS = period_ns_for(PAL_FRAME_CYCLES, CLOCK_6502_PAL);

// The injected clock. A pacer under test never waits: the fake sleep simply
// moves the clock to the deadline, which is what a perfect sleep would do.
int64_t g_now_ns = 0;
std::vector<int64_t> g_sleeps;

auto fake_now() -> int64_t { return g_now_ns; }

auto fake_sleep_until(int64_t deadline_ns) -> void {
  g_sleeps.push_back(deadline_ns);
  g_now_ns = deadline_ns;
}

/**
 * @brief RAII machine description and fake clock.
 *
 * g_state and g_current_clk_6502 are process globals, and so is the fake
 * clock the pacer is handed, so every case goes through this.
 */
class ScopedPacerWorld_t {
 public:
  ScopedPacerWorld_t(uint32_t clks_per_frame, double clock_hz)
      : previous_cycles_(g_state.clks_per_frame),
        previous_clock_(g_current_clk_6502) {
    g_state.clks_per_frame = clks_per_frame;
    g_current_clk_6502 = clock_hz;
    g_now_ns = 0;
    g_sleeps.clear();
  }

  ~ScopedPacerWorld_t() {
    g_state.clks_per_frame = previous_cycles_;
    g_current_clk_6502 = previous_clock_;
    g_now_ns = 0;
    g_sleeps.clear();
  }

  ScopedPacerWorld_t(const ScopedPacerWorld_t&) = delete;
  auto operator=(const ScopedPacerWorld_t&) -> ScopedPacerWorld_t& = delete;
  ScopedPacerWorld_t(ScopedPacerWorld_t&&) = delete;
  auto operator=(ScopedPacerWorld_t&&) -> ScopedPacerWorld_t& = delete;

 private:
  uint32_t previous_cycles_;
  double previous_clock_;
};

}  // namespace

TEST_CASE("Frame Pacer: The Period Comes From The Machine") {
  // Not a literal: the video standard decides how long a frame lasts, and
  // the 16 ms that used to be hardcoded is short of NTSC by 0.69 ms, which is
  // four percent of the audio the device asks for.
  {
    ScopedPacerWorld_t world(NTSC_FRAME_CYCLES, CLOCK_6502_NTSC);
    FramePacer_t pacer(fake_now, fake_sleep_until);
    CHECK(pacer.frame_period_ns() == NTSC_PERIOD_NS);
    // 16.688 ms, against the 16 ms that used to be hardcoded.
    CHECK(pacer.frame_period_ns() / 1000 == 16688);
  }
  {
    ScopedPacerWorld_t world(PAL_FRAME_CYCLES, CLOCK_6502_PAL);
    FramePacer_t pacer(fake_now, fake_sleep_until);
    CHECK(pacer.frame_period_ns() == PAL_PERIOD_NS);
    CHECK(pacer.frame_period_ns() / 1000 == 20000);
  }
}

TEST_CASE("Frame Pacer: A Machine That Has Not Spoken Yet Still Paces") {
  ScopedPacerWorld_t world(0, 0.0);
  FramePacer_t pacer(fake_now, fake_sleep_until);
  CHECK(pacer.frame_period_ns() == 16688000);
}

TEST_CASE("Frame Pacer: A Thousand Frames Accumulate Without Drift") {
  // The whole point of an accumulating deadline. A per-frame sleep of
  // round(period) would drift by the rounding every frame; against a deadline
  // the rounding cancels, so a thousand frames land exactly a thousand
  // periods later however the individual sleeps fall.
  ScopedPacerWorld_t world(NTSC_FRAME_CYCLES, CLOCK_6502_NTSC);
  FramePacer_t pacer(fake_now, fake_sleep_until);

  constexpr int frames = 1000;
  const int64_t start = g_now_ns;
  for (int frame = 0; frame < frames; ++frame) {
    pacer.wait_for_next_frame();
  }

  CHECK(g_sleeps.size() == frames);
  CHECK(g_now_ns - start == frames * NTSC_PERIOD_NS);
  for (int frame = 0; frame < frames; ++frame) {
    CHECK(g_sleeps[static_cast<size_t>(frame)] ==
          start + ((frame + 1) * NTSC_PERIOD_NS));
  }
}

TEST_CASE("Frame Pacer: Work Inside The Frame Comes Out Of The Sleep") {
  // A frame that took eight milliseconds to emulate and draw sleeps for the
  // rest of the period, not for a whole one. This is what the flat
  // SDL_Delay(16) could not do: its period was the sleep plus the work.
  ScopedPacerWorld_t world(NTSC_FRAME_CYCLES, CLOCK_6502_NTSC);
  FramePacer_t pacer(fake_now, fake_sleep_until);

  constexpr int64_t work_ns = 8000000;
  constexpr int frames = 100;
  const int64_t start = g_now_ns;
  for (int frame = 0; frame < frames; ++frame) {
    g_now_ns += work_ns;
    pacer.wait_for_next_frame();
  }

  // Exactly one period per frame, all of the work absorbed. The first
  // frame's work is outside the grid because the deadline is armed on the
  // first wait, which is the only moment the pacer knows anything.
  CHECK(g_now_ns - start == work_ns + (frames * NTSC_PERIOD_NS));

  // Half the period spent working is still one period per frame, and a frame
  // that overruns the period entirely is the case the resync covers.
  CHECK(work_ns < NTSC_PERIOD_NS);
}

TEST_CASE("Frame Pacer: A Frame Or Two Late Is Caught Up, Not Resynced") {
  // The deadline absorbs a hiccup by not sleeping until it has caught up,
  // which is how the average rate survives a slow frame.
  ScopedPacerWorld_t world(NTSC_FRAME_CYCLES, CLOCK_6502_NTSC);
  FramePacer_t pacer(fake_now, fake_sleep_until);

  const int64_t start = g_now_ns;
  pacer.wait_for_next_frame();

  // One frame that overran by two periods: the next two calls do not sleep.
  g_now_ns += 2 * NTSC_PERIOD_NS;
  const size_t sleeps_before = g_sleeps.size();
  pacer.wait_for_next_frame();
  pacer.wait_for_next_frame();
  CHECK(g_sleeps.size() == sleeps_before);

  // And the third is back on the original grid, not on a new one.
  pacer.wait_for_next_frame();
  CHECK(g_sleeps.size() == sleeps_before + 1);
  CHECK(g_sleeps.back() == start + (4 * NTSC_PERIOD_NS));
}

TEST_CASE("Frame Pacer: Falling Far Behind Starts Again From Now") {
  // Past a few frames the backlog is not worth catching up: running the
  // emulation fast to make it up would raise the pitch of everything queued.
  ScopedPacerWorld_t world(NTSC_FRAME_CYCLES, CLOCK_6502_NTSC);
  FramePacer_t pacer(fake_now, fake_sleep_until);

  pacer.wait_for_next_frame();

  // Ten periods of stall, well past the four-frame tolerance.
  g_now_ns += 10 * NTSC_PERIOD_NS;
  const int64_t stalled_at = g_now_ns;
  const size_t sleeps_before = g_sleeps.size();
  pacer.wait_for_next_frame();
  CHECK(g_sleeps.size() == sleeps_before);

  // The very next frame is a full period after the stall ended, so one late
  // frame costs one frame rather than ten.
  pacer.wait_for_next_frame();
  CHECK(g_sleeps.size() == sleeps_before + 1);
  CHECK(g_sleeps.back() == stalled_at + NTSC_PERIOD_NS);
}

TEST_CASE("Frame Pacer: Resync Drops The Deadline") {
  // What turbo does on the way out: the accumulated deadline describes a
  // past that no longer applies, so the next frame starts a new grid.
  ScopedPacerWorld_t world(NTSC_FRAME_CYCLES, CLOCK_6502_NTSC);
  FramePacer_t pacer(fake_now, fake_sleep_until);

  pacer.wait_for_next_frame();
  g_now_ns += 5 * NTSC_PERIOD_NS;

  pacer.resync();
  const int64_t resumed_at = g_now_ns;
  pacer.wait_for_next_frame();
  CHECK(g_sleeps.back() == resumed_at + NTSC_PERIOD_NS);
}

TEST_CASE("Frame Pacer: A Machine Type Change Takes Effect Next Frame") {
  // The period is read every frame rather than captured, because switching
  // between NTSC and PAL changes it and the loop does not restart.
  ScopedPacerWorld_t world(NTSC_FRAME_CYCLES, CLOCK_6502_NTSC);
  FramePacer_t pacer(fake_now, fake_sleep_until);

  pacer.wait_for_next_frame();
  const int64_t after_ntsc = g_now_ns;

  g_state.clks_per_frame = PAL_FRAME_CYCLES;
  g_current_clk_6502 = CLOCK_6502_PAL;
  pacer.wait_for_next_frame();
  CHECK(g_now_ns - after_ntsc == PAL_PERIOD_NS);
}
