#include <SDL/SDL.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include "apple2/SoundCore.h"
#include "apple2/Video.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
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

static auto SDLCALL sdl1AudioCallback(void* userdata, Uint8* stream, int len)
    -> void {
  (void)userdata;
  if (len <= 0) {
    return;
  }

  auto* temp_buf = reinterpret_cast<int16_t*>(stream);
  int num_samples = len / (static_cast<int>(sizeof(int16_t)));
  SoundCore_GetSamples(temp_buf, static_cast<size_t>(num_samples));

  if (g_audio_dumper.file != nullptr) {
    audio_dumper_put_samples(&g_audio_dumper, temp_buf,
                             static_cast<uint32_t>(num_samples));
  }
}

auto DSInit() -> bool {
  if (g_bDSAvailable) {
    return true;
  }

  SDL_AudioSpec desired;
  SDL_AudioSpec obtained;
  SDL_memset(&desired, 0, sizeof(desired));
  desired.freq = SPKR_SAMPLE_RATE;
  desired.channels = 2;
  desired.format = AUDIO_S16SYS;
  constexpr int AUDIO_SAMPLES = 1024;
  desired.samples = AUDIO_SAMPLES;
  desired.callback = sdl1AudioCallback;
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

  Linapple_SetAudioCallback([](const int16_t* samples,
                               size_t num_samples) -> void {
    SoundCore_UploadSpeakerSamples(samples, static_cast<uint32_t>(num_samples));
  });

  Linapple_SetMockAudioCallback(
      [](const int16_t* samples, size_t num_samples) -> void {
        SoundCore_UploadMockingboardSamples(samples,
                                            static_cast<uint32_t>(num_samples));
      });

  return true;
}

auto DSShutdown() -> void {
  if (g_audio_dumper.file != nullptr) {
    audio_dumper_finalize(&g_audio_dumper);
  }

  if (g_bDSAvailable) {
    SDL_CloseAudio();
    g_bDSAvailable = false;
  }
}

extern void SDL_HandleEvent(SDL_Event* e);

auto Sys_Input() -> void {
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    SDL_HandleEvent(&event);
  }
}

auto EnterMessageLoop() -> void {
  constexpr int APPLE2_FRAME_CYCLES = 17030;
  constexpr int TARGET_FRAME_MS = 16;
  while (g_state.mode != MODE_EXIT) {
    Sys_Input();

    Linapple_RunFrame(APPLE2_FRAME_CYCLES);
    DrawFrameWindow();
    SDL_Delay(TARGET_FRAME_MS);
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
  // a buffer and DSInit needs it later. Alternatively we could access
  // config.szAudioDumpPath directly but it's cleaner to keep the frontend's
  // specific state separate if it uses a heap string.
  if (config.szAudioDumpPath.at(0) != '\0') {
    g_pszAudioDumpFile = SDL_strdup(config.szAudioDumpPath.data());
  }

  if (SysInit() != 0) {
    return 1;
  }

  while (true) {
    AppController_SetRestart(false);

    if (SessionInit(&config) != 0) {
      break;
    }

    if (config.bBoot) {
      VideoRedrawScreen();
    }

    if (config.bBenchmark) {
      VideoBenchmark();
    } else {
      EnterMessageLoop();
    }

    SessionShutdown();
    if (!AppController_ShouldRestart()) {
      break;
    }
  }

  SysShutdown();
  return 0;
}
