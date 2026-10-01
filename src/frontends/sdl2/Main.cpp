// SPDX-License-Identifier: GPL-2.0-only
#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_version.h>
#include <SDL_stdinc.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

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
#include "frontends/common/sdl/AudioDevice.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/sdl2/Frame.h"

namespace {

SDL_AudioDeviceID g_audio_device = 0;
std::string g_audio_dump_file;
AudioDumper_t g_audio_dumper;

auto sdl2_audio_callback(void* userdata, Uint8* stream, int len) -> void {
  (void)userdata;
  if (len <= 0) {
    return;
  }

  auto* temp_buf = reinterpret_cast<int16_t*>(stream);
  const int num_samples = len / (static_cast<int>(sizeof(int16_t)));
  audio_mixer_get_samples(temp_buf, static_cast<size_t>(num_samples));

  if (audio_dumper_is_active(&g_audio_dumper)) {
    audio_dumper_put_samples(&g_audio_dumper, temp_buf,
                             static_cast<uint32_t>(num_samples));
  }
}

}  // namespace

auto ds_init() -> bool {
  if (g_audio_device != 0U) {
    return true;
  }

  // Opening the device at its own rate is what removes the OS-side resample:
  // the mixer's is then the only one in the chain. 44100 is the fallback for a
  // failed query, not a target.
  constexpr int k_fallback_rate_hz = 44100;
  int requested_rate = k_fallback_rate_hz;
#if SDL_VERSION_ATLEAST(2, 0, 15)
  SDL_AudioSpec native;
  SDL_zero(native);
  if (SDL_GetAudioDeviceSpec(0, 0, &native) == 0 && native.freq > 0) {
    requested_rate = native.freq;
  }
#endif

  SDL_AudioSpec desired;
  SDL_AudioSpec obtained;
  SDL_zero(desired);
  desired.freq = requested_rate;
  desired.channels = 2;
  desired.format = AUDIO_S16SYS;
  desired.samples = audio_device_buffer_samples(requested_rate);
  desired.callback = sdl2_audio_callback;
  desired.userdata = nullptr;

  g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
  if (g_audio_device == 0) {
    std::printf("Unable to open SDL audio: %s\n", SDL_GetError());
    return false;
  }

  const auto device_rate_hz = static_cast<uint32_t>(obtained.freq);

  if (!g_audio_dump_file.empty()) {
    audio_dumper_initialize(&g_audio_dumper, g_audio_dump_file.c_str(),
                            device_rate_hz, 2);
  }

  SDL_PauseAudioDevice(g_audio_device, 0);

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

  if (g_audio_device != 0U) {
    SDL_PauseAudioDevice(g_audio_device, 1);
    SDL_CloseAudioDevice(g_audio_device);
    g_audio_device = 0;
  }

  audio_mixer_destroy();

  if (audio_dumper_is_active(&g_audio_dumper)) {
    audio_dumper_finalize(&g_audio_dumper);
  }
}

auto sys_input() -> void {
  SDL_Event event{};
  while (SDL_PollEvent(&event) != 0) {
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

    const uint32_t cycles = linapple_get_frame_cycles();
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
  AppConfig_t& config = Configuration_t::instance();
  if (app_args_parse(argc, argv, &config) != 0) {
    return 1;
  }

  if (app_controller_handle_diagnostic_commands(&config)) {
    return 0;
  }

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
