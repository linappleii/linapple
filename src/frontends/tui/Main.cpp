#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "TuiAudio.h"
#include "TuiInput.h"
#include "TuiTerminal.h"
#include "TuiVideo.h"
#include "core/AudioMixer.h"
#include "core/LinAppleCore.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"

auto VideoCallback(const uint32_t* pixels, int width, int height, int pitch)
    -> void {
  tui_video_render_frame(pixels, width, height, pitch);
}

auto AudioCallback(const int16_t* samples, size_t num_samples) -> void {
  audio_mixer_upload_speaker_samples(samples,
                                     static_cast<uint32_t>(num_samples));
}

auto MockAudioCallback(const int16_t* samples, size_t num_samples) -> void {
  audio_mixer_upload_mockingboard_samples(samples,
                                          static_cast<uint32_t>(num_samples));
}

auto TitleCallback(const char* title) -> void { (void)title; }

auto main(int argc, char** argv) -> int {
  AppConfig_t config = {};
  if (app_args_parse(argc, argv, &config) != 0) {
    return 1;
  }

  if (app_controller_handle_diagnostic_commands(&config)) {
    return 0;
  }

  if (tui_terminal_initialize() != 0) {
    return 1;
  }

  if (app_controller_initialize(&config) != 0) {
    tui_terminal_shutdown();
    return 1;
  }

  tui_video_initialize();
  tui_video_set_render_mode(config.tui_render_mode);
  tui_input_initialize();
  tui_audio_initialize();

  linapple_set_video_callback(VideoCallback);
  linapple_set_audio_callback(AudioCallback);
  linapple_set_mock_audio_callback(MockAudioCallback);
  linapple_set_title_callback(TitleCallback);

  AppController_LoadInitialMedia(&config);

  constexpr int apple2_frame_cycles = 17030;
  constexpr auto frame_duration = std::chrono::microseconds(16650);

  auto next_frame = std::chrono::steady_clock::now();

  // Run until interrupted
  while (!tui_terminal_is_interrupted()) {
    if (tui_terminal_was_resized()) {
      tui_terminal_clear_resized();
      tui_video_on_resize();
    }

    tui_input_poll();

    linapple_run_frame(apple2_frame_cycles);

    next_frame += frame_duration;
    auto now = std::chrono::steady_clock::now();
    if (now < next_frame) {
      std::this_thread::sleep_until(next_frame);
    } else {
      next_frame = now;
    }
  }

  AppController_Shutdown();
  tui_audio_shutdown();
  tui_input_shutdown();
  tui_terminal_shutdown();

  return 0;
}
