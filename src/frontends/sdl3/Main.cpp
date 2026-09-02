#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

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
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/JoystickFrontend.h"

// SDL Audio Stream for Frontend
bool g_ds_available = false;
SDL_AudioStream* g_audioStream = nullptr;
static char* g_audio_dump_file = nullptr;
static AudioDumper_t g_audio_dumper;

static void SDLCALL sdl3_audio_callback(void* userdata, SDL_AudioStream* stream,
                                        int additional_amount,
                                        int total_amount) {
  (void)userdata;
  (void)total_amount;
  if (additional_amount <= 0) return;

  auto* temp_buf =
      static_cast<int16_t*>(SDL_malloc(static_cast<size_t>(additional_amount)));
  if (!temp_buf) return;

  int num_samples = additional_amount / (static_cast<int>(sizeof(int16_t)));
  audio_mixer_get_samples(temp_buf, static_cast<size_t>(num_samples));

  if (g_audio_dumper.is_active()) {
    audio_dumper_put_samples(&g_audio_dumper,
                             reinterpret_cast<int16_t*>(temp_buf),
                             static_cast<uint32_t>(num_samples));
  }

  SDL_PutAudioStreamData(stream, temp_buf, additional_amount);
  SDL_free(temp_buf);
}

auto ds_init() -> bool {
  if (g_audioStream) return true;

  SDL_AudioSpec desired;
  desired.freq = SPKR_SAMPLE_RATE;
  desired.channels = 2;
  desired.format = SDL_AUDIO_S16;

  if (g_audio_dump_file) {
    audio_dumper_initialize(&g_audio_dumper, g_audio_dump_file,
                            SPKR_SAMPLE_RATE, 2);
  }

  g_audioStream =
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired,
                                sdl3_audio_callback, nullptr);
  if (!g_audioStream) {
    return false;
  }

  SDL_ResumeAudioStreamDevice(g_audioStream);
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

void ds_shutdown() {
  if (g_audioStream) {
    SDL_DestroyAudioStream(g_audioStream);
    g_audioStream = nullptr;
  }

  if (g_audio_dumper.is_active()) {
    audio_dumper_finalize(&g_audio_dumper);
  }
}

extern void sdl_handle_event(SDL_Event* e);

void sys_input() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    sdl_handle_event(&event);
  }
}

void enter_message_loop() {
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
  // config.audio_dump_path.data() directly but it's cleaner to keep the
  // frontend's specific state separate if it uses a heap string.
  if (config.audio_dump_path.at(0) != '\0') {
    g_audio_dump_file = SDL_strdup(config.audio_dump_path.data());
  }

  if (sys_init() != 0) return 1;

  do {
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
  } while (app_controller_should_restart());

  sys_shutdown();
  return 0;
}
