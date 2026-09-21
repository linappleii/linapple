// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/FramePacer.h"

#include <chrono>
#include <cstdint>
#include <thread>

#include "core/LinAppleCore.h"

namespace {

// The NTSC frame period.
constexpr int64_t fallback_period_ns = 16688000;

// Falling a frame or two behind is ordinary -- a window resize, a disk seek,
// a scheduler hiccup -- and the accumulated deadline absorbs it by not
// sleeping until it has caught up. Beyond this the backlog is not something
// to catch up on: running the emulation fast to make up for it would raise
// the pitch of everything the speaker has queued.
constexpr int64_t resync_after_frames = 4;

auto steady_now_ns() -> int64_t {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

auto steady_sleep_until_ns(int64_t deadline_ns) -> void {
  const std::chrono::steady_clock::time_point deadline(
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::nanoseconds(deadline_ns)));
  std::this_thread::sleep_until(deadline);
}

}  // namespace

FramePacer_t::FramePacer_t()
    : now_(steady_now_ns), sleep_until_(steady_sleep_until_ns) {}

FramePacer_t::FramePacer_t(FrameClockNowFn_t now,
                           FrameClockSleepUntilFn_t sleep_until)
    : now_(now), sleep_until_(sleep_until) {}

auto FramePacer_t::frame_period_ns() const -> int64_t {
  // g_state.clks_per_frame and not linapple_get_frame_cycles(): the latter is
  // scaled by the emulation speed control, and pacing to it would turn a
  // request for double speed into a request for half the frame rate.
  const double cycles = static_cast<double>(g_state.clks_per_frame);
  const double clock_hz = g_current_clk_6502;
  if (!(cycles > 0.0) || !(clock_hz > 0.0)) {
    return fallback_period_ns;
  }
  return static_cast<int64_t>((cycles * 1e9) / clock_hz);
}

auto FramePacer_t::resync() -> void { armed_ = false; }

auto FramePacer_t::wait_for_next_frame() -> void {
  const int64_t period = frame_period_ns();
  const int64_t now = now_();

  if (!armed_) {
    deadline_ns_ = now;
    armed_ = true;
  }

  deadline_ns_ += period;

  if ((now - deadline_ns_) > (resync_after_frames * period)) {
    deadline_ns_ = now;
    return;
  }

  if (deadline_ns_ > now) {
    sleep_until_(deadline_ns_);
  }
}
