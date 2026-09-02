#include <SDL_audio.h>
#include <SDL_error.h>
#include <SDL_events.h>
#include <SDL_platform.h>
#include <SDL_stdinc.h>
#include <SDL_timer.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "AppConfig.h"
#include "apple2/Video.h"
#include "core/AudioMixer.h"
#include "core/LinAppleCore.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioDumper.h"
#include "frontends/common/Frontend.h"
#include "frontends/sdl2/Frame.h"
#include "frontends/sdl2/JoystickFrontend.h"

// SDL Audio Device for Frontend
bool g_ds_available = false;
SDL_AudioDeviceID g_audioDevice = 0;
static char* g_audio_dump_file = nullptr;
static AudioDumper_t g_audio_dumper;

static auto SDLCALL sdl2AudioCallback(void* userdata, Uint8* stream, int len)
    -> void {
  (void)userdata;
  if (len <= 0) {
    return;
  }

  auto* temp_buf = reinterpret_cast<int16_t*>(stream);
  int num_samples = len / (static_cast<int>(sizeof(int16_t)));
  audio_mixer_get_samples(temp_buf, static_cast<size_t>(num_samples));

  if (g_audio_dumper.is_active()) {
    audio_dumper_put_samples(&g_audio_dumper, temp_buf,
                             static_cast<uint32_t>(num_samples));
  }
}

auto ds_init() -> bool {
  if (g_audioDevice != 0u) {
    return true;
  }

  SDL_AudioSpec desired;
  SDL_AudioSpec obtained;
  SDL_zero(desired);
  desired.freq = SPKR_SAMPLE_RATE;
  desired.channels = 2;
  desired.format = AUDIO_S16SYS;
  constexpr int audio_samples = 1024;
  desired.samples = audio_samples;
  desired.callback = sdl2AudioCallback;
  desired.userdata = nullptr;

  if (g_audio_dump_file != nullptr) {
    audio_dumper_initialize(&g_audio_dumper, g_audio_dump_file,
                            SPKR_SAMPLE_RATE, 2);
  }

  g_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
  if (g_audioDevice == 0) {
    printf("Unable to open SDL audio: %s\n", SDL_GetError());
    return false;
  }

  SDL_PauseAudioDevice(g_audioDevice, 0);
  g_ds_available = true;

  linapple_set_audio_callback(
      [](const int16_t* samples, size_t num_samples) -> void {
        audio_mixer_upload_speaker_samples(samples,
                                           static_cast<uint32_t>(num_samples));
      });

  linapple_set_mock_audio_callback(
      [](const int16_t* samples, size_t num_samples) -> void {
        audio_mixer_upload_mockingboard_samples(
            samples, static_cast<uint32_t>(num_samples));
      });

  return true;
}

auto ds_shutdown() -> void {
  if (g_audioDevice != 0u) {
    SDL_CloseAudioDevice(g_audioDevice);
    g_audioDevice = 0;
  }

  if (g_audio_dumper.is_active()) {
    audio_dumper_finalize(&g_audio_dumper);
  }
}

extern void sdl_handle_event(SDL_Event* e);

auto sys_input() -> void {
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    sdl_handle_event(&event);
  }
}

auto enter_message_loop() -> void {
  constexpr int apple2_frame_cycles = 17030;
  constexpr int target_frame_ms = 16;
  while (g_state.mode != MODE_EXIT) {
    sys_input();
    joy_frontend_update();

    linapple_run_frame(apple2_frame_cycles);
    draw_frame_window();
    SDL_Delay(target_frame_ms);
  }
}

auto main(int argc, char** argv) -> int {
  AppConfig_t config = {};
  if (app_args_parse(argc, argv, &config) != 0) {
    return 1;
  }

  if (app_controller_handle_diagnostic_commands(&config)) {
    return 0;
  }

  // Store the audio dump file name explicitly since AppConfig_t only holds it
  // in a buffer and ds_init needs it later. Alternatively we could access
  // config.audio_dump_path directly but it's cleaner to keep the frontend's
  // specific state separate if it uses a heap string.
  if (config.audio_dump_path.at(0) != '\0') {
    g_audio_dump_file = SDL_strdup(config.audio_dump_path.data());
  }

  if (sys_init() != 0) {
    return 1;
  }

  while (true) {
    app_controller_set_restart(false);

    if (session_init(&config) != 0) {
      break;
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
    if (!app_controller_should_restart()) {
      break;
    }
  }

  sys_shutdown();
  return 0;
}
