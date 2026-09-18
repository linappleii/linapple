// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/AudioDumper.h"
#include "frontends/common/AudioMixer.h"
#include "test_fixtures_core.h"

auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                     uint32_t cycles) -> uint8_t;

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;
using TestFixtures::ScopedCore_t;

constexpr uint16_t ADDR_SPEAKER = 0xC030;
constexpr uint32_t NTSC_FRAME_CYCLES = 17030;
constexpr uint32_t DEVICE_RATE_HZ = 48000;

// A half period of 510 cycles is 1 kHz at the NTSC clock.
constexpr uint32_t HALF_PERIOD_CYCLES = 510;
constexpr uint32_t TONE_FRAMES = 60;
constexpr uint32_t SILENT_FRAMES = 60;

// One emulated frame is 17030 / (1020484 / 48000) = 801.03 output frames.
// Draining 800 per frame leaves about sixty in the ring when the tone stops,
// which is why the decay fit skips the head of the silent second.
constexpr size_t FRAMES_PER_EMULATED_FRAME = 800;
constexpr size_t DECAY_FIT_SKIP = 600;

// The mixer's 21.26-cycle output window smears the one-cycle 2.0 onset edge,
// so the unsmeared 32767 is unreachable at 48000. mean(a^k, k = 0..21).
constexpr int PEAK_PCM = 32752;
// 2 / (1 + a^510) * 0.5 * 32767 * 0.99954
constexpr int PLATEAU_PCM = 16558;
// a^(1020484/48000): the cone's per-cycle decay resolved to one output frame.
constexpr double DECAY_PER_FRAME = 0.999076;

std::vector<float> g_tapped;
size_t g_tap_calls = 0;

auto record_tap(const char* peripheral_id, int slot,
                const float* const* channels, size_t num_channels,
                size_t num_samples) -> void {
  (void)peripheral_id;
  (void)slot;
  (void)num_channels;
  g_tap_calls++;
  g_tapped.insert(g_tapped.end(), channels[0], channels[0] + num_samples);
}

/**
 * @brief The whole chain from the soft switch to interleaved PCM.
 *
 * Peripheral, host, core, announce, mixer, resampler, gain and clip, wired
 * exactly as sdl2/Main.cpp wires them. No mocks, no device, no files.
 */
class SpeakerChain_t {
 public:
  explicit SpeakerChain_t(const TestConfig_t& config) : core_(config) {
    g_tapped.clear();
    g_tap_calls = 0;

    audio_mixer_initialize(DEVICE_RATE_HZ);
    audio_mixer_set_channel_tap_callback(record_tap);

    linapple_set_audio_source_register_callback(
        [](int slot, const char* peripheral_id,
           const PeripheralAudioInfo_t* info) -> void {
          audio_mixer_register_source(slot, peripheral_id, info);
        });
    linapple_set_audio_source_unregister_callback(
        [](int slot) -> void { audio_mixer_unregister_source(slot); });
    linapple_set_audio_channel_callback(
        [](const char* peripheral_id, int slot, const float* const* channels,
           size_t num_channels, size_t num_samples) -> void {
          audio_mixer_upload_channels(peripheral_id, slot, channels,
                                      num_channels,
                                      static_cast<uint32_t>(num_samples));
        });

    registered_ = (peripheral_register(speaker_get_descriptor(), 0) == 0);
  }

  ~SpeakerChain_t() {
    audio_mixer_set_channel_tap_callback(nullptr);
    audio_mixer_destroy();
    g_tapped.clear();
    g_tap_calls = 0;
  }

  SpeakerChain_t(const SpeakerChain_t&) = delete;
  auto operator=(const SpeakerChain_t&) -> SpeakerChain_t& = delete;
  SpeakerChain_t(SpeakerChain_t&&) = delete;
  auto operator=(SpeakerChain_t&&) -> SpeakerChain_t& = delete;

  auto registered() const -> bool { return registered_; }

  /**
   * @brief Run one emulated frame, toggling every `period` cycles.
   *
   * A period of zero leaves the soft switch alone. Cycle offsets are absolute
   * across the whole run because cpu_calc_cycles measures forward from where
   * the last instruction left the counter.
   */
  auto run_frame(uint32_t period) -> void {
    const uint64_t frame_end = cycle_ + NTSC_FRAME_CYCLES;
    // next_toggle_ carries across the frame boundary: 17030 is not a whole
    // number of half periods, and restarting the count each frame would put a
    // wrong-length period into the tone sixty times.
    while (period > 0 && next_toggle_ < frame_end) {
      io_map_dispatch(0, ADDR_SPEAKER, 0, 0,
                      static_cast<uint32_t>(next_toggle_));
      next_toggle_ += period;
    }
    cycle_ = frame_end;
    cpu_calc_cycles(static_cast<uint32_t>(cycle_));
    peripheral_manager_think(NTSC_FRAME_CYCLES);

    std::vector<int16_t> block(FRAMES_PER_EMULATED_FRAME * 2, 0);
    audio_mixer_get_samples(block.data(), block.size());
    output_.insert(output_.end(), block.begin(), block.end());
  }

  auto output() const -> const std::vector<int16_t>& { return output_; }

  static auto left(const std::vector<int16_t>& stereo, size_t frame)
      -> int16_t {
    return stereo[frame * 2];
  }

 private:
  ScopedCore_t core_;
  bool registered_ = false;
  uint64_t cycle_ = 0;
  uint64_t next_toggle_ = HALF_PERIOD_CYCLES;
  std::vector<int16_t> output_;
};

auto count_zero_crossings(const std::vector<int16_t>& stereo, size_t first,
                          size_t count) -> size_t {
  size_t crossings = 0;
  for (size_t i = first; i + 1 < first + count; ++i) {
    const bool was_negative = SpeakerChain_t::left(stereo, i) < 0;
    const bool is_negative = SpeakerChain_t::left(stereo, i + 1) < 0;
    crossings += static_cast<size_t>(was_negative != is_negative);
  }
  return crossings;
}

// The run is the artifact a human listens to. It is written only when the
// environment names a path, so nothing in the test depends on the file.
auto write_wav_if_requested(const std::vector<int16_t>& stereo) -> void {
  const char* path = std::getenv("LINAPPLE_E2E_WAV");
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  AudioDumper_t dumper;
  if (audio_dumper_initialize(&dumper, path, DEVICE_RATE_HZ, 2) != 0) {
    return;
  }
  audio_dumper_put_samples(&dumper, stereo.data(),
                           static_cast<uint32_t>(stereo.size()));
  audio_dumper_finalize(&dumper);
}

}  // namespace

TEST_CASE("Speaker End To End: A 1 kHz Tone Through The Whole Chain") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  SpeakerChain_t chain(config);
  REQUIRE(chain.registered());

  for (uint32_t frame = 0; frame < TONE_FRAMES; ++frame) {
    chain.run_frame(HALF_PERIOD_CYCLES);
  }
  const size_t tone_frames = chain.output().size() / 2;

  for (uint32_t frame = 0; frame < SILENT_FRAMES / 2; ++frame) {
    chain.run_frame(0);
  }
  const size_t calls_after_decay = g_tap_calls;
  for (uint32_t frame = 0; frame < SILENT_FRAMES / 2; ++frame) {
    chain.run_frame(0);
  }
  // A cone at rest synthesizes nothing, so the tap stops being called at all.
  CHECK(g_tap_calls == calls_after_decay);

  const std::vector<int16_t>& out = chain.output();
  REQUIRE(tone_frames == TONE_FRAMES * FRAMES_PER_EMULATED_FRAME);
  write_wav_if_requested(out);

  // Pitch. A mixer that resolved the speaker's cycle-domain rate as if it
  // were 44100 Hz would be off by a factor of twenty-three.
  const size_t crossings = count_zero_crossings(out, 0, tone_frames);
  CHECK(crossings >= 1999);
  CHECK(crossings <= 2001);

  // Peak. The onset edge is a step of 2.0, the mixer's default gain for a
  // peak_magnitude of 2.0 is 0.5, and the output window smears the one-cycle
  // edge across 21.26 cycles. The best-aligned onset is what survives.
  int16_t peak = 0;
  for (size_t i = 0; i < tone_frames; ++i) {
    peak = std::max<int16_t>(peak, SpeakerChain_t::left(out, i));
  }
  CHECK(peak >= PEAK_PCM - 1);
  CHECK(peak <= PEAK_PCM + 1);

  // Plateau, over the last fifty periods of the tone. The post-edge peak has
  // long since converged on 2 / (1 + a^510), and the same window smear
  // applies.
  const size_t plateau_window = 50 * DEVICE_RATE_HZ / 1000;
  int16_t plateau = 0;
  for (size_t i = tone_frames - plateau_window; i < tone_frames; ++i) {
    plateau = std::max<int16_t>(plateau, SpeakerChain_t::left(out, i));
  }
  CHECK(plateau >= PLATEAU_PCM - 1);
  CHECK(plateau <= PLATEAU_PCM + 1);

  // Drop-off. Once the toggling stops the DC blocker is the only thing left,
  // and its per-cycle decay resolves to a^21.26 per output frame. The fit
  // runs from the head of the silent second down to where integer
  // quantization starts to dominate the ratio.
  const size_t fit_start = tone_frames + DECAY_FIT_SKIP;
  const size_t total_frames = out.size() / 2;
  size_t fit_end = fit_start;
  while (fit_end + 1 < total_frames &&
         std::abs(SpeakerChain_t::left(out, fit_end)) > 1000) {
    ++fit_end;
  }
  REQUIRE(fit_end > fit_start + 1000);

  for (size_t i = fit_start; i + 1 < fit_end; ++i) {
    CHECK(static_cast<double>(SpeakerChain_t::left(out, i + 1)) ==
          doctest::Approx(static_cast<double>(SpeakerChain_t::left(out, i)) *
                          DECAY_PER_FRAME)
              .epsilon(1e-3));
  }

  // And then true silence: the cone snaps to zero at its epsilon, the mixer
  // drains what is left, and nothing follows.
  const size_t tail_start = total_frames - (10 * FRAMES_PER_EMULATED_FRAME);
  for (size_t i = tail_start; i < total_frames; ++i) {
    CHECK(SpeakerChain_t::left(out, i) == 0);
    CHECK(out[(i * 2) + 1] == 0);
  }

  // Single clip. The tap sits ahead of gain and the conversion, so what it
  // saw is what the peripheral emitted: an unclipped 2.0 onset, which is
  // above full scale and which only the mixer brings down.
  REQUIRE(g_tap_calls > 0);
  const auto onset = std::find_if(g_tapped.begin(), g_tapped.end(),
                                  [](float v) -> bool { return v != 0.0f; });
  REQUIRE(onset != g_tapped.end());
  CHECK(*onset == doctest::Approx(2.0f));

  float loudest = 0.0f;
  for (float sample : g_tapped) {
    loudest = std::max(loudest, std::fabs(sample));
  }
  CHECK(loudest == doctest::Approx(2.0f));
}
