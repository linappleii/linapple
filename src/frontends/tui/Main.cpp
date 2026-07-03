#include <unistd.h>

#include <iostream>

#include "TuiAudio.h"
#include "TuiInput.h"
#include "TuiTerminal.h"
#include "TuiVideo.h"
#include "core/AudioMixer.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"

auto VideoCallback(const uint32_t* pixels, int width, int height, int pitch)
    -> void {
  TuiVideo_RenderFrame(pixels, width, height, pitch);
}

auto AudioCallback(const int16_t* samples, size_t num_samples) -> void {
  audio_mixer_upload_speaker_samples(samples, static_cast<uint32_t>(num_samples));
}

auto MockAudioCallback(const int16_t* samples, size_t num_samples) -> void {
  audio_mixer_upload_mockingboard_samples(samples,
                                          static_cast<uint32_t>(num_samples));
}

auto TitleCallback(const char* title) -> void { (void)title; }

auto main(int argc, char* argv[]) -> int {
  AppConfig config = {};
  if (AppArgs_Parse(argc, argv, &config) != 0) {
    return 1;
  }

  if (AppController_HandleDiagnosticCommands(&config)) {
    return 0;
  }

  if (TuiTerminal_Initialize() != 0) {
    return 1;
  }

  if (AppController_Initialize(&config) != 0) {
    TuiTerminal_Shutdown();
    return 1;
  }

  TuiVideo_Initialize();
  TuiInput_Initialize();
  TuiAudio_Initialize();

  Linapple_SetVideoCallback(VideoCallback);
  Linapple_SetAudioCallback(AudioCallback);
  Linapple_SetMockAudioCallback(MockAudioCallback);
  Linapple_SetTitleCallback(TitleCallback);

  AppController_LoadInitialMedia(&config);

  constexpr int APPLE2_FRAME_CYCLES = 17030;
  constexpr int TARGET_FRAME_MS = 16;
  constexpr int MS_TO_US = 1000;

  // Run until interrupted
  while (!TuiTerminal_IsInterrupted()) {
    if (TuiTerminal_WasResized()) {
      TuiTerminal_ClearResized();
      TuiVideo_OnResize();
    }

    TuiInput_Poll();

    Linapple_RunFrame(APPLE2_FRAME_CYCLES);
    usleep(TARGET_FRAME_MS * MS_TO_US);
  }

  AppController_Shutdown();
  TuiAudio_Shutdown();
  TuiInput_Shutdown();
  TuiTerminal_Shutdown();

  return 0;
}
