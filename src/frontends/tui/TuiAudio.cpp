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
static pa_simple* g_pa_handle = nullptr;
#endif

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#include <alsa/pcm.h>
static snd_pcm_t* g_alsa_handle = nullptr;
#endif

namespace {

enum class AudioDriver_t : uint8_t { none, pulse, alsa, bell };
static AudioDriver_t g_driver = AudioDriver_t::none;

static std::atomic<bool> g_audio_running(false);
static std::thread g_audio_thread;

static constexpr size_t k_chunk_frames = 512;
static constexpr size_t k_channels = 2;
static constexpr int k_pace_sleep_ms = 10;
static constexpr int k_buffer_ms = 40;
static constexpr int k_req_ms = 10;
static constexpr uint32_t k_usec_per_msec = 1000;

// Neither backend here offers a native-format query the way SDL2 and SDL3 do:
// pa_simple has none and resamples server side, so this is the request rather
// than a target. ALSA may configure something else, and that comes back
// through snd_pcm_hw_params_current.
static constexpr unsigned int k_fallback_rate_hz = 44100;

static auto audio_thread_func() -> void {
  std::array<int16_t, k_chunk_frames * k_channels> stereo_buffer{};

  while (g_audio_running) {
    audio_mixer_get_samples(stereo_buffer.data(), stereo_buffer.size());

    if (g_driver == AudioDriver_t::pulse) {
#ifdef HAVE_PULSE_SIMPLE
      int error = 0;
      if (pa_simple_write(g_pa_handle, stereo_buffer.data(),
                          stereo_buffer.size() * sizeof(int16_t), &error) < 0) {
      }
#endif
    } else if (g_driver == AudioDriver_t::alsa) {
#ifdef HAVE_ALSA
      snd_pcm_sframes_t frames =
          snd_pcm_writei(g_alsa_handle, stereo_buffer.data(), k_chunk_frames);
      if (frames < 0) {
        snd_pcm_recover(g_alsa_handle, static_cast<int>(frames), 1);
      }
#endif
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(k_pace_sleep_ms));
    }
  }
}

}  // namespace

auto tui_audio_initialize() -> void {
  if (g_audio_running) {
    return;
  }

  unsigned int device_rate_hz = k_fallback_rate_hz;

#ifdef HAVE_PULSE_SIMPLE
  pa_sample_spec ss;
  ss.format = PA_SAMPLE_S16LE;
  ss.channels = k_channels;
  ss.rate = k_fallback_rate_hz;

  pa_buffer_attr attr;
  attr.maxlength = static_cast<uint32_t>(-1);
  attr.tlength = static_cast<uint32_t>(pa_usec_to_bytes(
      static_cast<pa_usec_t>(k_buffer_ms) * k_usec_per_msec, &ss));
  attr.prebuf = static_cast<uint32_t>(-1);
  attr.minreq = static_cast<uint32_t>(pa_usec_to_bytes(
      static_cast<pa_usec_t>(k_req_ms) * k_usec_per_msec, &ss));
  attr.fragsize = static_cast<uint32_t>(-1);

  int error = 0;
  g_pa_handle =
      pa_simple_new(nullptr, "LinApple-TUI", PA_STREAM_PLAYBACK, nullptr,
                    "emulation", &ss, nullptr, &attr, &error);
  if (g_pa_handle) {
    g_driver = AudioDriver_t::pulse;
  }
#endif

#ifdef HAVE_ALSA
  if (g_driver == AudioDriver_t::none) {
    if (snd_pcm_open(&g_alsa_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) >=
        0) {
      snd_pcm_set_params(g_alsa_handle, SND_PCM_FORMAT_S16_LE,
                         SND_PCM_ACCESS_RW_INTERLEAVED, k_channels,
                         k_fallback_rate_hz, 1, k_buffer_ms * k_usec_per_msec);

      snd_pcm_hw_params_t* hw_params = nullptr;
      if (snd_pcm_hw_params_malloc(&hw_params) >= 0) {
        unsigned int configured_rate = 0;
        int rate_dir = 0;
        if (snd_pcm_hw_params_current(g_alsa_handle, hw_params) >= 0 &&
            snd_pcm_hw_params_get_rate(hw_params, &configured_rate,
                                       &rate_dir) >= 0 &&
            configured_rate > 0) {
          device_rate_hz = configured_rate;
        }
        snd_pcm_hw_params_free(hw_params);
      }

      g_driver = AudioDriver_t::alsa;
    }
  }
#endif

  if (g_driver == AudioDriver_t::none) {
    g_driver = AudioDriver_t::bell;
  }

  // The mixer has to know the rate before the draining thread starts asking
  // it for samples.
  audio_mixer_initialize(device_rate_hz);

  g_audio_running = true;
  g_audio_thread = std::thread(audio_thread_func);
}

auto tui_audio_shutdown() -> void {
  g_audio_running = false;
  if (g_audio_thread.joinable()) {
    g_audio_thread.join();
  }

#ifdef HAVE_PULSE_SIMPLE
  if (g_pa_handle) {
    pa_simple_free(g_pa_handle);
    g_pa_handle = nullptr;
  }
#endif

#ifdef HAVE_ALSA
  if (g_alsa_handle) {
    snd_pcm_close(g_alsa_handle);
    g_alsa_handle = nullptr;
  }
#endif

  audio_mixer_destroy();

  g_driver = AudioDriver_t::none;
}
