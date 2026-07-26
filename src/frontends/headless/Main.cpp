#include <iostream>

#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
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
  AppConfig_t config = {};
  if (app_args_parse(argc, argv, &config) != 0) {
    return 1;
  }

  if (app_controller_handle_diagnostic_commands(&config)) {
    return 0;
  }

  if (app_controller_initialize(&config) != 0) {
    return 1;
  }

  std::cout << "Starting LinApple Headless Frontend…" << std::endl;

  linapple_set_video_callback(VideoCallback);
  linapple_set_audio_callback(AudioCallback);
  linapple_set_title_callback(TitleCallback);

  AppController_LoadInitialMedia(&config);

  constexpr int HEADLESS_FRAMES = 60;
  constexpr int apple2_frame_cycles = 17030;

  for (int i = 0; i < HEADLESS_FRAMES; ++i) {
    linapple_run_frame(apple2_frame_cycles);
  }

  AppController_Shutdown();

  std::cout << "Headless execution complete." << std::endl;

  return 0;
}
