// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

// Nanoseconds on a monotonic clock. Injected so the deadline arithmetic can
// be tested without any real time passing.
using FrameClockNowFn_t = int64_t (*)();
using FrameClockSleepUntilFn_t = void (*)(int64_t deadline_ns);

// Paces the emulation loop to match the machine's frame timing (NTSC/PAL).
class FramePacer_t {
 public:
  FramePacer_t();
  FramePacer_t(FrameClockNowFn_t now, FrameClockSleepUntilFn_t sleep_until);

  // One emulated frame of wall time, re-read every frame because a machine
  // type change moves it.
  auto frame_period_ns() const -> int64_t;

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
