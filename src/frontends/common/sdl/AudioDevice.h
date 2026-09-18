// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "frontends/common/sdl/SdlCompat.h"

// The device buffer is a latency figure, so it is expressed in time. 1024
// frames was 23 ms only at 44100. SDL prefers a power of two, so this rounds to
// the nearest one rather than to the exact millisecond count.
//
// SDL1 and SDL2 only: SDL3's SDL_AudioSpec carries format, channels and freq
// and nothing else, so the device buffer there is the library's to size.
auto audio_device_buffer_samples(int rate_hz) -> Uint16;
