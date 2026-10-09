// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/FramePacer.h"

#include <chrono>
#include <cstdint>
#include <thread>

#include "core/LinAppleCore.h"

namespace {

// The NTSC frame period (16.688 ms).
constexpr int64_t fallback_period_ns = 16688000;

// Maximum number of backlog frames to catch up on before re-synchronizing.
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

FramePacer::FramePacer()
    : now_(steady_now_ns), sleep_until_(steady_sleep_until_ns) {}

FramePacer::FramePacer(FrameClockNowFn now, FrameClockSleepUntilFn sleep_until)
    : now_(now != nullptr ? now : steady_now_ns),
      sleep_until_(sleep_until != nullptr ? sleep_until
                                          : steady_sleep_until_ns) {}

auto FramePacer::frame_period_ns() -> int64_t {
  const double cycles = static_cast<double>(system_state.clks_per_frame);
  const double clock_hz = current_clk_6502;
  if (!(cycles > 0.0) || !(clock_hz > 0.0)) {
    return fallback_period_ns;
  }
  return static_cast<int64_t>((cycles * 1e9) / clock_hz);
}

auto FramePacer::resync() -> void { armed_ = false; }

auto FramePacer::wait_for_next_frame() -> void {
  const int64_t period = frame_period_ns();
  const int64_t now = (now_ != nullptr) ? now_() : steady_now_ns();

  if (!armed_) {
    deadline_ns_ = now;
    armed_ = true;
  }

  deadline_ns_ += period;

  if ((now - deadline_ns_) > (resync_after_frames * period)) {
    deadline_ns_ = now;
    return;
  }

  if (deadline_ns_ > now && sleep_until_ != nullptr) {
    sleep_until_(deadline_ns_);
  }
}

auto frame_pacer_init(FramePacer* pacer, FrameClockNowFn now,
                      FrameClockSleepUntilFn sleep_until) -> void {
  if (pacer == nullptr) {
    return;
  }
  *pacer = FramePacer(now, sleep_until);
}

auto frame_pacer_wait(FramePacer* pacer) -> void {
  if (pacer == nullptr) {
    return;
  }
  pacer->wait_for_next_frame();
}

auto frame_pacer_resync(FramePacer* pacer) -> void {
  if (pacer == nullptr) {
    return;
  }
  pacer->resync();
}

auto frame_pacer_period_ns(const FramePacer* pacer) -> int64_t {
  if (pacer == nullptr) {
    return fallback_period_ns;
  }
  return pacer->frame_period_ns();
}
