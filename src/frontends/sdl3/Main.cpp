#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include "apple2/Riff.h"
#include "apple2/SaveState.h"
#include "apple2/SoundCore.h"
#include "apple2/Video.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppController.h"
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/Frontend.h"

// SDL Audio Stream for Frontend
bool g_bDSAvailable = false;
SDL_AudioStream* g_audioStream = nullptr;
static char* g_pszAudioDumpFile = nullptr;

static void SDLCALL sdl3AudioCallback(void* userdata, SDL_AudioStream* stream,
                                      int additional_amount, int total_amount) {
  (void)userdata;
  (void)total_amount;
  if (additional_amount <= 0) return;

  auto* temp_buf =
      static_cast<int16_t*>(SDL_malloc(static_cast<size_t>(additional_amount)));
  if (!temp_buf) return;

  int num_samples = additional_amount / (static_cast<int>(sizeof(int16_t)));
  SoundCore_GetSamples(temp_buf, static_cast<size_t>(num_samples));

  if (g_pszAudioDumpFile) {
    RiffPutSamples(reinterpret_cast<int16_t*>(temp_buf),
                   static_cast<uint32_t>(num_samples));
  }

  SDL_PutAudioStreamData(stream, temp_buf, additional_amount);
  SDL_free(temp_buf);
}

auto DSInit() -> bool {
  if (g_audioStream) return true;

  SDL_AudioSpec desired;
  desired.freq = SPKR_SAMPLE_RATE;
  desired.channels = 2;
  desired.format = SDL_AUDIO_S16;

  if (g_pszAudioDumpFile) {
    RiffInitWriteFile(g_pszAudioDumpFile, SPKR_SAMPLE_RATE, 2);
  }

  g_audioStream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, sdl3AudioCallback, nullptr);
  if (g_audioStream == nullptr) {
    printf("Unable to open SDL audio: %s\n", SDL_GetError());
    return false;
  }

  SDL_ResumeAudioStreamDevice(g_audioStream);
  g_bDSAvailable = true;

  Linapple_SetAudioCallback(
      [](const int16_t* samples, size_t num_samples) -> void {
        DSUploadBuffer(samples, static_cast<uint32_t>(num_samples));
      });

  return true;
}

void DSShutdown() {
  if (g_pszAudioDumpFile) {
    RiffFinishWriteFile();
  }

  if (g_audioStream) {
    SDL_DestroyAudioStream(g_audioStream);
    g_audioStream = nullptr;
  }
}

extern void SDL_HandleEvent(SDL_Event* e);

void Sys_Input() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    SDL_HandleEvent(&event);
  }
}

void EnterMessageLoop() {
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
  // config.szAudioDumpPath.data() directly but it's cleaner to keep the
  // frontend's specific state separate if it uses a heap string.
  if (config.szAudioDumpPath.data()[0] != '\0') {
    g_pszAudioDumpFile = SDL_strdup(config.szAudioDumpPath.data());
  }

  if (SysInit() != 0) return 1;

  do {
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
  } while (AppController_ShouldRestart());

  SysShutdown();
  return 0;
}
