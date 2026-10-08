// SPDX-License-Identifier: GPL-2.0-only
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioDumper.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/FramePacer.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/SdlPtr.h"

namespace {

SdlAudioStreamPtr_t g_audio_stream;
std::string g_audio_dump_file;
AudioDumper_t g_audio_dumper;

auto SDLCALL sdl3_audio_callback(void* userdata, SDL_AudioStream* stream,
                                 int additional_amount, int total_amount)
    -> void {
  (void)userdata;
  (void)total_amount;
  if (additional_amount <= 0) {
    return;
  }

  constexpr size_t k_max_stack_samples = 4096;
  std::array<int16_t, k_max_stack_samples> stack_buf{};
  std::vector<int16_t> heap_buf;
  int16_t* temp_buf = stack_buf.data();
  const auto num_samples =
      static_cast<size_t>(additional_amount / sizeof(int16_t));
  if (num_samples > k_max_stack_samples) {
    heap_buf.resize(num_samples);
    temp_buf = heap_buf.data();
  }

  audio_mixer_get_samples(temp_buf, num_samples);

  if (audio_dumper_is_active(&g_audio_dumper)) {
    audio_dumper_put_samples(&g_audio_dumper, temp_buf,
                             static_cast<uint32_t>(num_samples));
  }

  SDL_PutAudioStreamData(stream, temp_buf, additional_amount);
}

}  // namespace

auto ds_init() -> bool {
  if (g_audio_stream != nullptr) {
    return true;
  }

  // Asking for the device's own rate is what removes SDL3's converting stage:
  // the mixer's resample is then the only one in the chain. 44100 is the
  // fallback for a failed query, not a target.
  constexpr int fallback_rate_hz = 44100;
  int device_rate = fallback_rate_hz;
  SDL_AudioSpec native;
  int native_frames = 0;
  if (SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &native,
                               &native_frames) &&
      native.freq > 0) {
    device_rate = native.freq;
  }

  SDL_AudioSpec desired;
  desired.freq = device_rate;
  desired.channels = 2;
  desired.format = SDL_AUDIO_S16;

  g_audio_stream.reset(
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired,
                                sdl3_audio_callback, nullptr));
  if (g_audio_stream == nullptr) {
    return false;
  }

  // SDL3 honours the stream's source spec exactly, so what was asked for is
  // what the callback is fed.
  const auto device_rate_hz = static_cast<uint32_t>(device_rate);

  if (!g_audio_dump_file.empty()) {
    audio_dumper_initialize(&g_audio_dumper, g_audio_dump_file.c_str(),
                            device_rate_hz, 2);
  }

  SDL_ResumeAudioStreamDevice(g_audio_stream.get());

  audio_mixer_initialize(device_rate_hz);

  linapple_set_audio_source_register_callback(
      [](int slot, const char* peripheral_id,
         const PeripheralAudioInfo_t* info) -> void {
        audio_mixer_register_source(slot, peripheral_id, info);
      });

  linapple_set_audio_source_unregister_callback(
      [](int slot) -> void { audio_mixer_unregister_source(slot); });

  linapple_set_audio_channel_callback(
      [](const char* peripheral_id, int slot, const float* const* channels,
         size_t num_channels, size_t num_samples) -> void {
        audio_mixer_upload_channels(peripheral_id, slot, channels, num_channels,
                                    static_cast<uint32_t>(num_samples));
      });

  return true;
}

auto ds_shutdown() -> void {
  linapple_set_audio_channel_callback(nullptr);
  linapple_set_audio_source_register_callback(nullptr);
  linapple_set_audio_source_unregister_callback(nullptr);

  if (g_audio_stream != nullptr) {
    SDL_PauseAudioStreamDevice(g_audio_stream.get());
    g_audio_stream.reset();
  }

  audio_mixer_destroy();

  if (audio_dumper_is_active(&g_audio_dumper)) {
    audio_dumper_finalize(&g_audio_dumper);
  }
}

auto sys_input() -> void {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    sdl_handle_event(&event);
  }
}

auto enter_message_loop() -> void {
  FramePacer_t pacer;
  while (system_state.mode != app_mode_exit) {
    sys_input();
    if (system_state.mode == app_mode_exit) {
      break;
    }
    joy_frontend_update();

    if (system_state.reset_timing) {
      pacer.resync();
      system_state.reset_timing = false;
    }

    uint32_t cycles = linapple_get_frame_cycles();
    linapple_run_frame(cycles);
    draw_frame_window();

    if (!linapple_get_turbo()) {
      pacer.wait_for_next_frame();
    } else {
      pacer.resync();
      SDL_Delay(0);
    }
  }
}

auto main(int argc, char** argv) -> int {
  AppConfig& config = Configuration::instance();
  if (app_args_parse(argc, argv, &config) != 0) {
    return 1;
  }

  if (app_controller_handle_diagnostic_commands(&config)) {
    return 0;
  }

  // Store the audio dump file name explicitly since AppConfig only holds it
  // in a buffer and ds_init needs it later.
  if (config.audio_dump_path.at(0) != '\0') {
    g_audio_dump_file = config.audio_dump_path.data();
  }

  if (sys_init() != 0) {
    return 1;
  }

  do {
    app_controller_set_restart(false);

    if (session_init(&config) != 0) {
      sys_shutdown();
      return 1;
    }

    if (config.is_boot) {
      video_redraw_screen();
    }

    if (config.is_benchmark) {
      video_benchmark();
    } else {
      enter_message_loop();
    }

    session_shutdown();
  } while (app_controller_should_restart());

  sys_shutdown();
  return 0;
}
