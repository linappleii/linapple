#include "TuiAudio.h"

#include <pulse/sample.h>
#include <pulse/def.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include "core/AudioMixer.h"

#ifdef HAVE_PULSE_SIMPLE
#include <pulse/simple.h>
static pa_simple* g_pa_handle = nullptr;
#endif

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
static snd_pcm_t* g_alsa_handle = nullptr;
#endif

enum class AudioDriver { None, Pulse, Alsa, Bell };
static AudioDriver g_driver = AudioDriver::None;

static std::atomic<bool> g_audio_running(false);
static std::thread g_audio_thread;

static constexpr size_t chunk_frames = 512;
static constexpr size_t channels = 2;
static constexpr int pace_sleep_ms = 10;
static constexpr int buffer_ms = 40;
static constexpr int req_ms = 10;

static void AudioThreadFunc() {
  std::array<int16_t, chunk_frames> mono_buffer{};
  std::array<int16_t, chunk_frames * channels> stereo_buffer{};

  while (g_audio_running) {
    audio_mixer_get_samples(mono_buffer.data(), chunk_frames);

    for (size_t i = 0; i < chunk_frames; ++i) {
      stereo_buffer.at(i * 2) = mono_buffer.at(i);
      stereo_buffer.at(i * 2 + 1) = mono_buffer.at(i);
    }

    if (g_driver == AudioDriver::Pulse) {
#ifdef HAVE_PULSE_SIMPLE
      int error = 0;
      if (pa_simple_write(g_pa_handle, stereo_buffer.data(),
                          stereo_buffer.size() * sizeof(int16_t), &error) < 0) {
      }
#endif
    } else if (g_driver == AudioDriver::Alsa) {
#ifdef HAVE_ALSA
      snd_pcm_sframes_t frames =
          snd_pcm_writei(g_alsa_handle, stereo_buffer.data(), chunk_frames);
      if (frames < 0) {
        snd_pcm_prepare(g_alsa_handle);
      }
#endif
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(pace_sleep_ms));
    }
  }
}

auto tui_audio_initialize() -> void {
#ifdef HAVE_PULSE_SIMPLE
  pa_sample_spec ss;
  ss.format = PA_SAMPLE_S16LE;
  ss.channels = channels;
  ss.rate = sample_rate;

  pa_buffer_attr attr;
  attr.maxlength = static_cast<uint32_t>(-1);
  attr.tlength = static_cast<uint32_t>(pa_usec_to_bytes(buffer_ms * 1000, &ss));
  attr.prebuf = static_cast<uint32_t>(-1);
  attr.minreq = static_cast<uint32_t>(pa_usec_to_bytes(req_ms * 1000, &ss));
  attr.fragsize = static_cast<uint32_t>(-1);

  int error = 0;
  g_pa_handle =
      pa_simple_new(nullptr, "LinApple-TUI", PA_STREAM_PLAYBACK, nullptr,
                    "emulation", &ss, nullptr, &attr, &error);
  if (g_pa_handle) {
    g_driver = AudioDriver::Pulse;
  }
#endif

#ifdef HAVE_ALSA
  if (g_driver == AudioDriver::None) {
    if (snd_pcm_open(&g_alsa_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) >=
        0) {
      snd_pcm_set_params(g_alsa_handle, SND_PCM_FORMAT_S16_LE,
                         SND_PCM_ACCESS_RW_INTERLEAVED, channels, sample_rate,
                         1, buffer_ms * 1000);
      g_driver = AudioDriver::Alsa;
    }
  }
#endif

  if (g_driver == AudioDriver::None) {
    g_driver = AudioDriver::Bell;
  }

  g_audio_running = true;
  g_audio_thread = std::thread(AudioThreadFunc);
}

auto tui_audio_process_samples(const int16_t* samples, size_t num_samples)
    -> void {
  (void)samples;
  (void)num_samples;
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

  g_driver = AudioDriver::None;
}
