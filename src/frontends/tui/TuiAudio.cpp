// SPDX-License-Identifier: GPL-2.0-only
#include "TuiAudio.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "frontends/common/AudioMixer.h"

#ifdef HAVE_PULSE_SIMPLE
#include <pulse/def.h>
#include <pulse/sample.h>
#include <pulse/simple.h>
static pa_simple* pa_handle = nullptr;
#endif

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>  // IWYU pragma: keep
#include <alsa/pcm.h>
static snd_pcm_t* alsa_handle = nullptr;
#endif

namespace {

enum class AudioDriver : uint8_t { none, pulse, alsa, bell };
static AudioDriver audio_driver = AudioDriver::none;

static std::atomic<bool> audio_running(false);
static std::thread audio_thread;

static constexpr size_t chunk_frames = 512;
static constexpr size_t channels = 2;
static constexpr int pace_sleep_ms = 10;
static constexpr int buffer_ms = 40;
static constexpr int req_ms = 10;
static constexpr uint32_t usec_per_msec = 1000;

// Neither backend here offers a native-format query the way SDL2 and SDL3 do:
// pa_simple has none and resamples server side, so this is the request rather
// than a target. ALSA may configure something else, and that comes back
// through snd_pcm_hw_params_current.
static constexpr unsigned int fallback_rate_hz = 44100;

static auto audio_thread_func() -> void {
  std::array<int16_t, chunk_frames * channels> stereo_buffer{};

  while (audio_running) {
    audio_mixer_get_samples(stereo_buffer.data(), stereo_buffer.size());

    if (audio_driver == AudioDriver::pulse) {
#ifdef HAVE_PULSE_SIMPLE
      int error = 0;
      if (pa_simple_write(pa_handle, stereo_buffer.data(),
                          stereo_buffer.size() * sizeof(int16_t), &error) < 0) {
      }
#endif
    } else if (audio_driver == AudioDriver::alsa) {
#ifdef HAVE_ALSA
      snd_pcm_sframes_t frames =
          snd_pcm_writei(alsa_handle, stereo_buffer.data(), chunk_frames);
      if (frames < 0) {
        snd_pcm_recover(alsa_handle, static_cast<int>(frames), 1);
      }
#endif
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(pace_sleep_ms));
    }
  }
}

}  // namespace

auto tui_audio_initialize() -> void {
  if (audio_running) {
    return;
  }

  unsigned int device_rate_hz = fallback_rate_hz;

#ifdef HAVE_PULSE_SIMPLE
  pa_sample_spec ss;
  ss.format = PA_SAMPLE_S16LE;
  ss.channels = channels;
  ss.rate = fallback_rate_hz;

  pa_buffer_attr attr;
  attr.maxlength = static_cast<uint32_t>(-1);
  attr.tlength = static_cast<uint32_t>(pa_usec_to_bytes(
      static_cast<pa_usec_t>(buffer_ms) * usec_per_msec, &ss));
  attr.prebuf = static_cast<uint32_t>(-1);
  attr.minreq = static_cast<uint32_t>(pa_usec_to_bytes(
      static_cast<pa_usec_t>(req_ms) * usec_per_msec, &ss));
  attr.fragsize = static_cast<uint32_t>(-1);

  int error = 0;
  pa_handle =
      pa_simple_new(nullptr, "LinApple-TUI", PA_STREAM_PLAYBACK, nullptr,
                    "emulation", &ss, nullptr, &attr, &error);
  if (pa_handle) {
    audio_driver = AudioDriver::pulse;
  }
#endif

#ifdef HAVE_ALSA
  if (audio_driver == AudioDriver::none) {
    if (snd_pcm_open(&alsa_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) >=
        0) {
      snd_pcm_set_params(alsa_handle, SND_PCM_FORMAT_S16_LE,
                         SND_PCM_ACCESS_RW_INTERLEAVED, channels,
                         fallback_rate_hz, 1, buffer_ms * usec_per_msec);

      snd_pcm_hw_params_t* hw_params = nullptr;
      if (snd_pcm_hw_params_malloc(&hw_params) >= 0) {
        unsigned int configured_rate = 0;
        int rate_dir = 0;
        if (snd_pcm_hw_params_current(alsa_handle, hw_params) >= 0 &&
            snd_pcm_hw_params_get_rate(hw_params, &configured_rate,
                                       &rate_dir) >= 0 &&
            configured_rate > 0) {
          device_rate_hz = configured_rate;
        }
        snd_pcm_hw_params_free(hw_params);
      }

      audio_driver = AudioDriver::alsa;
    }
  }
#endif

  if (audio_driver == AudioDriver::none) {
    audio_driver = AudioDriver::bell;
  }

  // The mixer has to know the rate before the draining thread starts asking
  // it for samples.
  audio_mixer_initialize(device_rate_hz);

  audio_running = true;
  audio_thread = std::thread(audio_thread_func);
}

auto tui_audio_shutdown() -> void {
  audio_running = false;
  if (audio_thread.joinable()) {
    audio_thread.join();
  }

#ifdef HAVE_PULSE_SIMPLE
  if (pa_handle) {
    pa_simple_free(pa_handle);
    pa_handle = nullptr;
  }
#endif

#ifdef HAVE_ALSA
  if (alsa_handle) {
    snd_pcm_close(alsa_handle);
    alsa_handle = nullptr;
  }
#endif

  audio_mixer_destroy();

  audio_driver = AudioDriver::none;
}
