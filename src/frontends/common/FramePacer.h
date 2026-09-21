// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

// Nanoseconds on a monotonic clock. Injected so the deadline arithmetic can
// be tested without any real time passing.
using FrameClockNowFn_t = int64_t (*)();
using FrameClockSleepUntilFn_t = void (*)(int64_t deadline_ns);

/**
 * @brief Holds the emulation loop to the machine's own frame time.
 *
 * Audio production is a function of how fast this loop runs: one emulated
 * frame is a fixed number of 6502 cycles and the speaker emits one sample per
 * cycle, so a loop slower than the machine hands the mixer less audio per
 * wall second than the device consumes, and every callback underruns for as
 * long as the deficit lasts. A flat sleep cannot hold a rate at all -- its
 * real period is the sleep plus however long input, emulation and rendering
 * took -- so the deadline accumulates instead and the sleep is whatever is
 * left of it.
 *
 * The period comes from the machine rather than a literal because the video
 * standard decides it: 16.688 ms at NTSC, 20.000 ms at PAL.
 */
class FramePacer_t {
 public:
  FramePacer_t();
  FramePacer_t(FrameClockNowFn_t now, FrameClockSleepUntilFn_t sleep_until);

  // One emulated frame of wall time, re-read every frame because a machine
  // type change moves it.
  auto frame_period_ns() const -> int64_t;

  // Call once per loop iteration, after the frame has been run and drawn.
  auto wait_for_next_frame() -> void;

  // Turbo, a debugger stop, or anything else that leaves the accumulated
  // deadline describing a past that no longer applies.
  auto resync() -> void;

 private:
  FrameClockNowFn_t now_;
  FrameClockSleepUntilFn_t sleep_until_;
  int64_t deadline_ns_ = 0;
  bool armed_ = false;
};
