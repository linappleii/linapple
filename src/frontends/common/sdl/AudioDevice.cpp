// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/sdl/AudioDevice.h"

#include <cstdint>

auto audio_device_buffer_samples(int rate_hz) -> uint16_t {
  constexpr int device_buffer_ms = 23;
  const int wanted = (rate_hz * device_buffer_ms) / 1000;
  int samples = 256;
  while (samples * 2 < wanted) {
    samples *= 2;
  }
  if ((wanted - samples) > ((samples * 2) - wanted)) {
    samples *= 2;
  }
  return static_cast<uint16_t>(samples);
}
