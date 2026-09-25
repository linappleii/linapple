// SPDX-License-Identifier: GPL-2.0-only
#include "test_fixtures.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "apple2/peripherals/Peripheral_Audio.h"
#include "core/LinAppleCore.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioMixer.h"
#include "test_fixtures_core.h"

auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                     uint32_t cycles) -> uint8_t;

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr uint16_t ADDR_SPEAKER = 0xC030;
constexpr uint32_t NTSC_FRAME_CYCLES = 17030;
constexpr uint32_t DEVICE_RATE_HZ = 48000;
constexpr float EDGE_POSITIVE = 2.0f;

// One emulated frame is 17030 / (1020484 / 48000) = 801.03 output frames, so
// 800 is what a drain can take without outrunning production.
constexpr size_t DRAINED_FRAMES = 800;

struct Announcement_t {
  int slot = -1;
  std::string id;
  PeripheralAudioInfo_t info{};
};

std::vector<Announcement_t> g_announcements;
size_t g_channel_calls = 0;
int g_channel_slot = -1;
std::string g_channel_id;
std::vector<float> g_channel_samples;

auto record_announce(int slot, const char* peripheral_id,
                     const PeripheralAudioInfo_t* info) -> void {
  Announcement_t record;
  record.slot = slot;
  record.id = (peripheral_id != nullptr) ? peripheral_id : "";
  record.info = *info;
  g_announcements.push_back(record);
  audio_mixer_register_source(slot, peripheral_id, info);
}

// The frontends' channel callback does the upload and nothing else; the
// recording either side of it is this suite's only window onto the call.
auto record_channel(const char* peripheral_id, int slot,
                    const float* const* channels, size_t num_channels,
                    size_t num_samples) -> void {
  g_channel_calls++;
  g_channel_slot = slot;
  g_channel_id = (peripheral_id != nullptr) ? peripheral_id : "";
  g_channel_samples.insert(g_channel_samples.end(), channels[0],
                           channels[0] + num_samples);
  audio_mixer_upload_channels(peripheral_id, slot, channels, num_channels,
                              static_cast<uint32_t>(num_samples));
}

/**
 * @brief A frontend's own startup sequence and nothing else.
 *
 * sdl1, sdl2 and sdl3 reach it through session_init, which calls
 * app_controller_initialize and only then ds_init; tui/Main.cpp and
 * headless/Main.cpp spell the same order out inline. Every audio callback is
 * therefore installed against a core that already holds its peripherals, and
 * a callback that only hears about future registrations hears nothing at all.
 */
class ScopedFrontend_t {
 public:
  explicit ScopedFrontend_t(const TestConfig_t& config) {
    g_announcements.clear();
    g_channel_calls = 0;
    g_channel_slot = -1;
    g_channel_id.clear();
    g_channel_samples.clear();

    app_config_default(&app_config_);
    util_safe_strcpy(app_config_.config_path.data(), config.c_str(),
                     path_max_len);
    initialized_ = (app_controller_initialize(&app_config_) == 0);

    audio_mixer_initialize(DEVICE_RATE_HZ);
    linapple_set_audio_source_register_callback(record_announce);
    linapple_set_audio_source_unregister_callback(
        [](int slot) -> void { audio_mixer_unregister_source(slot); });
    linapple_set_audio_channel_callback(record_channel);
  }

  ~ScopedFrontend_t() {
    linapple_set_audio_channel_callback(nullptr);
    linapple_set_audio_source_register_callback(nullptr);
    linapple_set_audio_source_unregister_callback(nullptr);
    audio_mixer_destroy();
    app_controller_shutdown();
  }

  ScopedFrontend_t(const ScopedFrontend_t&) = delete;
  auto operator=(const ScopedFrontend_t&) -> ScopedFrontend_t& = delete;
  ScopedFrontend_t(ScopedFrontend_t&&) = delete;
  auto operator=(ScopedFrontend_t&&) -> ScopedFrontend_t& = delete;

  auto initialized() const -> bool { return initialized_; }

 private:
  TestFixtures::ScopedCpuContext_t cpu_;
  AppConfig_t app_config_{};
  bool initialized_ = false;
};

auto non_zero_frames(const std::vector<int16_t>& stereo) -> size_t {
  size_t count = 0;
  for (size_t i = 0; i < stereo.size(); i += 2) {
    count += static_cast<size_t>(stereo[i] != 0);
  }
  return count;
}

}  // namespace

TEST_CASE("Frontend Init Order: A Late Subscriber Hears The Speaker") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedFrontend_t frontend(config);
  REQUIRE(frontend.initialized());

  REQUIRE(g_announcements.size() == 1);
  CHECK(g_announcements[0].slot == 0);
  CHECK(g_announcements[0].id == "linapple.speaker");
  CHECK(g_announcements[0].info.time_base == peripheral_audio_cpu_clocked);
  CHECK(g_announcements[0].info.cycle_divisor == 1);
  CHECK(g_announcements[0].info.num_channels == 1);
  CHECK(g_announcements[0].info.peak_magnitude == doctest::Approx(2.0f));
}

TEST_CASE("Frontend Init Order: A Strobe And A Frame Reach The Device") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  ScopedFrontend_t frontend(config);
  REQUIRE(frontend.initialized());

  io_map_dispatch(0, ADDR_SPEAKER, 0, 0, 0);
  cpu_calc_cycles(NTSC_FRAME_CYCLES);
  peripheral_manager_think(NTSC_FRAME_CYCLES);

  REQUIRE(g_channel_calls == 1);
  CHECK(g_channel_slot == 0);
  CHECK(g_channel_id == "linapple.speaker");
  // The speaker declares one sample per 6502 cycle, so a frame's worth of
  // thinking is a frame's worth of samples, and the first one is the edge.
  REQUIRE(g_channel_samples.size() == NTSC_FRAME_CYCLES);
  CHECK(g_channel_samples[0] == EDGE_POSITIVE);

  // The mixer only routes a slot it was told about. Draining it is what
  // separates "the callback fired" from "the machine made a sound": without
  // the replay the samples arrive and are dropped, and this is silence.
  std::vector<int16_t> out(DRAINED_FRAMES * 2, 0);
  audio_mixer_get_samples(out.data(), out.size());
  CHECK(non_zero_frames(out) == DRAINED_FRAMES);
}
