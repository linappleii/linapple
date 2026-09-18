// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/sdl/AudioDevice.h"

auto audio_device_buffer_samples(int rate_hz) -> Uint16 {
  constexpr int device_buffer_ms = 23;
  const int wanted = (rate_hz * device_buffer_ms) / 1000;
  int samples = 256;
  while (samples * 2 < wanted) {
    samples *= 2;
  }
  // samples and samples*2 bracket wanted; take whichever is closer.
  if ((wanted - samples) > ((samples * 2) - wanted)) {
    samples *= 2;
  }
  return static_cast<Uint16>(samples);
}
