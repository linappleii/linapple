#include <SDL/SDL.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include "core/AudioMixer.h"
#include "apple2/Video.h"
#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Log.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioDumper.h"
#include "frontends/sdl1/Frame.h"
#include "frontends/sdl1/Frontend.h"

// SDL Audio Device for Frontend
bool g_bDSAvailable = false;
static char* g_pszAudioDumpFile = nullptr;
static AudioDumper_t g_audio_dumper;

static auto SDLCALL sdl1_audio_callback(void* userdata, Uint8* stream, int len)
    -> void {
  (void)userdata;
  if (len <= 0) {
    return;
  }

  auto* temp_buf = reinterpret_cast<int16_t*>(stream);
  int num_samples = len / (static_cast<int>(sizeof(int16_t)));
  audio_mixer_get_samples(temp_buf, static_cast<size_t>(num_samples));

  if (g_audio_dumper.file != nullptr) {
    audio_dumper_put_samples(&g_audio_dumper, temp_buf,
                             static_cast<uint32_t>(num_samples));
  }
}

auto ds_init() -> bool {
  if (g_bDSAvailable) {
    return true;
  }

  SDL_AudioSpec desired;
  SDL_AudioSpec obtained;
  SDL_memset(&desired, 0, sizeof(desired));
  desired.freq = SPKR_SAMPLE_RATE;
  desired.channels = 2;
  desired.format = AUDIO_S16SYS;
  constexpr int audio_samples = 1024;
  desired.samples = audio_samples;
  desired.callback = sdl1_audio_callback;
  desired.userdata = nullptr;

  if (g_pszAudioDumpFile != nullptr) {
    audio_dumper_initialize(&g_audio_dumper, g_pszAudioDumpFile,
                            SPKR_SAMPLE_RATE, 2);
  }

  if (SDL_OpenAudio(&desired, &obtained) < 0) {
    printf("Unable to open SDL audio: %s\n", SDL_GetError());
    return false;
  }

  SDL_PauseAudio(0);
  g_bDSAvailable = true;

  linapple_set_audio_callback([](const int16_t* samples,
                               size_t num_samples) -> void {
    audio_mixer_upload_speaker_samples(samples, static_cast<uint32_t>(num_samples));
  });

  linapple_set_mock_audio_callback(
      [](const int16_t* samples, size_t num_samples) -> void {
        audio_mixer_upload_mockingboard_samples(samples,
                                            static_cast<uint32_t>(num_samples));
      });

  return true;
}

auto ds_shutdown() -> void {
  if (g_audio_dumper.file != nullptr) {
    audio_dumper_finalize(&g_audio_dumper);
  }

  if (g_bDSAvailable) {
    SDL_CloseAudio();
    g_bDSAvailable = false;
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

    linapple_run_frame(apple2_frame_cycles);
    DrawFrameWindow();
    SDL_Delay(target_frame_ms);
  }
}

auto main(int argc, char* argv[]) -> int {
  AppConfig config = {};
  if (AppArgs_Parse(argc, argv, &config) != 0) {
    return 1;
  }

  if (AppController_HandleDiagnosticCommands(&config)) {
    return 0;
  }

  // Store the audio dump file name explicitly since AppConfig only holds it in
  // a buffer and ds_init needs it later. Alternatively we could access
  // config.szAudioDumpPath directly but it's cleaner to keep the frontend's
  // specific state separate if it uses a heap string.
  if (config.szAudioDumpPath.at(0) != '\0') {
    g_pszAudioDumpFile = SDL_strdup(config.szAudioDumpPath.data());
  }

  if (sys_init() != 0) {
    return 1;
  }

  while (true) {
    AppController_SetRestart(false);

    if (session_init(&config) != 0) {
      break;
    }

    if (config.bBoot) {
      VideoRedrawScreen();
    }

    if (config.bBenchmark) {
      VideoBenchmark();
    } else {
      enter_message_loop();
    }

    SessionShutdown();
    if (!AppController_ShouldRestart()) {
      break;
    }
  }

  SysShutdown();
  return 0;
}
