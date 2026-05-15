#include <iostream>

#include "core/LinAppleCore.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"

auto VideoCallback(const uint32_t* pixels, int width, int height,
                   int pitch) -> void {  // NOLINT
  (void)pixels;
  (void)width;
  (void)height;
  (void)pitch;
}

auto AudioCallback(const int16_t* samples, size_t num_samples) -> void {
  (void)samples;
  (void)num_samples;
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
