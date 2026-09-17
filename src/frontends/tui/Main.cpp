// SPDX-License-Identifier: GPL-2.0-only
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "TuiAudio.h"
#include "TuiInput.h"
#include "TuiTerminal.h"
#include "TuiVideo.h"
#include "core/LinAppleCore.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioMixer.h"

auto video_callback(const uint32_t* pixels, int width, int height, int pitch)
    -> void {
  tui_video_render_frame(pixels, width, height, pitch);
}

auto audio_source_register_callback(int slot, const char* peripheral_id,
                                    const PeripheralAudioInfo_t* info) -> void {
  audio_mixer_register_source(slot, peripheral_id, info);
}

auto audio_source_unregister_callback(int slot) -> void {
  audio_mixer_unregister_source(slot);
}

auto audio_channel_callback(const char* peripheral_id, int slot,
                            const int16_t* const* channels, size_t num_channels,
                            size_t num_samples) -> void {
  audio_mixer_upload_channels(peripheral_id, slot, channels, num_channels,
                              static_cast<uint32_t>(num_samples));
}

auto title_callback(const char* title) -> void { (void)title; }

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

  do {
    app_controller_set_restart(false);

    if (app_controller_initialize(&config) != 0) {
      tui_terminal_shutdown();
      return 1;
    }

    tui_video_initialize();
    tui_video_set_render_mode(config.tui_render_mode);
    tui_input_initialize();
    tui_audio_initialize();

    audio_mixer_initialize();

    linapple_set_video_callback(video_callback);
    linapple_set_audio_channel_callback(audio_channel_callback);
    linapple_set_audio_source_register_callback(audio_source_register_callback);
    linapple_set_audio_source_unregister_callback(
        audio_source_unregister_callback);
    linapple_set_title_callback(title_callback);

    app_controller_load_initial_media(&config);

    constexpr auto frame_duration = std::chrono::microseconds(16650);

    auto next_frame = std::chrono::steady_clock::now();

    // Run until interrupted or restart requested
    while (!tui_terminal_is_interrupted() && !app_controller_should_restart()) {
      if (tui_terminal_was_resized()) {
        tui_terminal_clear_resized();
        tui_video_on_resize();
      }

      tui_input_poll();

      if (g_state.mode == MODE_DEBUG) {
        tui_video_render_frame(nullptr, 0, 0, 0);
      } else {
        uint32_t cycles = linapple_get_frame_cycles();
        linapple_run_frame(cycles);
      }

      if (!linapple_get_turbo()) {
        next_frame += frame_duration;
        auto now = std::chrono::steady_clock::now();
        if (now < next_frame) {
          std::this_thread::sleep_until(next_frame);
        } else {
          next_frame = now;
        }
      } else {
        std::this_thread::yield();
        next_frame = std::chrono::steady_clock::now();
      }
    }

    app_controller_shutdown();
    linapple_set_audio_channel_callback(nullptr);
    linapple_set_audio_source_register_callback(nullptr);
    linapple_set_audio_source_unregister_callback(nullptr);
    tui_audio_shutdown();
    audio_mixer_destroy();
    tui_input_shutdown();
  } while (app_controller_should_restart() && !tui_terminal_is_interrupted());

  tui_terminal_shutdown();

  return 0;
}
