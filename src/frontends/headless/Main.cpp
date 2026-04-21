#include <iostream>

#include "frontends/common/AppArgs.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"
#include "core/LinAppleCore.h"

void VideoCallback(const uint32_t* pixels, int width, int height, int pitch) {
  (void)pixels;
  (void)width;
  (void)height;
  (void)pitch;
}

void AudioCallback(const int16_t* samples, size_t num_samples) {
  (void)samples;
  (void)num_samples;
}

void TitleCallback(const char* title) { (void)title; }

auto main(int argc, char* argv[]) -> int {
  AppConfig config = {};
  if (AppArgs_Parse(argc, argv, &config) != 0) {
    return 1;
  }

  if (AppController_HandleDiagnosticCommands(&config)) {
    return 0;
  }

  if (AppController_Initialize(&config) != 0) {
    return 1;
  }

  std::cout << "Starting LinApple Headless Frontend…" << std::endl;

  Linapple_SetVideoCallback(VideoCallback);
  Linapple_SetAudioCallback(AudioCallback);
  Linapple_SetTitleCallback(TitleCallback);

  AppController_LoadInitialMedia(&config);

  constexpr int HEADLESS_FRAMES = 60;
  constexpr int APPLE2_FRAME_CYCLES = 17030;

  for (int i = 0; i < HEADLESS_FRAMES; ++i) {
    Linapple_RunFrame(APPLE2_FRAME_CYCLES);
  }

  AppController_Shutdown();

  std::cout << "Headless execution complete." << std::endl;

  return 0;
}
