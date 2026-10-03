// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

// The device buffer is a latency figure, so it is expressed in time. 1024
// frames was 23 ms only at 44100. SDL prefers a power of two, so this rounds to
// the nearest one rather than to the exact millisecond count.
[[nodiscard]] auto audio_device_buffer_samples(int rate_hz) noexcept
    -> uint16_t;
