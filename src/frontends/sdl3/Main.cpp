#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include "apple2/Video.h"
#include "core/AudioMixer.h"
#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Log.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioDumper.h"
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/Frontend.h"

// SDL Audio Stream for Frontend
bool g_bDSAvailable = false;
SDL_AudioStream* g_audioStream = nullptr;
static char* g_pszAudioDumpFile = nullptr;
static AudioDumper_t g_audio_dumper;

static void SDLCALL sdl3_audio_callback(void* userdata, SDL_AudioStream* stream,
                                      int additional_amount, int total_amount) {
  (void)userdata;
  (void)total_amount;
  if (additional_amount <= 0) return;

  auto* temp_buf =
      static_cast<int16_t*>(SDL_malloc(static_cast<size_t>(additional_amount)));
  if (!temp_buf) return;

  int num_samples = additional_amount / (static_cast<int>(sizeof(int16_t)));
  audio_mixer_get_samples(temp_buf, static_cast<size_t>(num_samples));

  if (g_audio_dumper.file) {
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

  if (g_pszAudioDumpFile) {
    audio_dumper_initialize(&g_audio_dumper, g_pszAudioDumpFile,
                            SPKR_SAMPLE_RATE, 2);
  }

  g_audioStream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, sdl3_audio_callback, nullptr);
  if (g_audioStream == nullptr) {
    printf("Unable to open SDL audio: %s\n", SDL_GetError());
    return false;
  }

  SDL_ResumeAudioStreamDevice(g_audioStream);
  g_bDSAvailable = true;

  Linapple_SetAudioCallback([](const int16_t* samples,
                               size_t num_samples) -> void {
    audio_mixer_upload_speaker_samples(samples, static_cast<uint32_t>(num_samples));
  });

  Linapple_SetMockAudioCallback(
      [](const int16_t* samples, size_t num_samples) -> void {
        audio_mixer_upload_mockingboard_samples(samples,
                                            static_cast<uint32_t>(num_samples));
      });

  return true;
}

void DSShutdown() {
  if (g_audio_dumper.file) {
    audio_dumper_finalize(&g_audio_dumper);
  }

  if (g_audioStream) {
    SDL_DestroyAudioStream(g_audioStream);
    g_audioStream = nullptr;
  }
}

extern void sdl_handle_event(SDL_Event* e);

void Sys_Input() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    sdl_handle_event(&event);
  }
}

void EnterMessageLoop() {
  constexpr int apple2_frame_cycles = 17030;
  constexpr int target_frame_ms = 16;
  while (g_state.mode != MODE_EXIT) {
    Sys_Input();

    Linapple_RunFrame(apple2_frame_cycles);
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
  // config.szAudioDumpPath.data() directly but it's cleaner to keep the
  // frontend's specific state separate if it uses a heap string.
  if (config.szAudioDumpPath.data()[0] != '\0') {
    g_pszAudioDumpFile = SDL_strdup(config.szAudioDumpPath.data());
  }

  if (sys_init() != 0) return 1;

  do {
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
      EnterMessageLoop();
    }

    SessionShutdown();
  } while (AppController_ShouldRestart());

  SysShutdown();
  return 0;
}
