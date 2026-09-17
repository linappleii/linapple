// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/AudioMixer.h"

namespace {

constexpr const char* SOURCE_ID = "test.source";
constexpr uint32_t NTSC_FRAME_CYCLES = 17030;

// The mixer's two latency literals, repeated here so that changing either one
// in AudioMixer.cpp fails the capacity and cushion case rather than silently
// moving the backlog.
constexpr size_t MIXER_CAPACITY_MS = 370;
constexpr size_t MIXER_CUSHION_MS = 140;

// The underrun fade's per-frame step, the normalized equivalent of the
// 800-per-frame step the mixer has always used.
constexpr int FADE_STEP_PCM = 800;

auto absolute_info(uint32_t rate_hz, uint32_t num_channels, float peak)
    -> PeripheralAudioInfo_t {
  PeripheralAudioInfo_t info{};
  info.time_base = peripheral_audio_absolute;
  info.sample_rate = rate_hz;
  info.num_channels = num_channels;
  info.peak_magnitude = peak;
  for (uint32_t c = 0; c < num_channels; ++c) {
    info.channels[c].default_pan_left = 1.0f;
    info.channels[c].default_pan_right = 1.0f;
  }
  return info;
}

auto cpu_clocked_info(uint32_t divisor, uint32_t num_channels, float peak)
    -> PeripheralAudioInfo_t {
  PeripheralAudioInfo_t info{};
  info.time_base = peripheral_audio_cpu_clocked;
  info.cycle_divisor = divisor;
  info.num_channels = num_channels;
  info.peak_magnitude = peak;
  for (uint32_t c = 0; c < num_channels; ++c) {
    info.channels[c].default_pan_left = 1.0f;
    info.channels[c].default_pan_right = 1.0f;
  }
  return info;
}

auto square_wave(double rate_hz, double tone_hz, size_t count)
    -> std::vector<float> {
  const double half_period = rate_hz / (2.0 * tone_hz);
  std::vector<float> wave(count);
  for (size_t i = 0; i < count; ++i) {
    const auto half =
        static_cast<long long>(static_cast<double>(i) / half_period);
    wave[i] = ((half % 2) == 0) ? 1.0f : -1.0f;
  }
  return wave;
}

/**
 * @brief RAII mixer at a declared output rate and 6502 clock.
 *
 * g_slots and g_current_clk_6502 are process globals, so every case goes
 * through the fixture: it owns the initialize/destroy pair and restores the
 * clock it borrowed.
 */
class MixerFixture_t {
 public:
  MixerFixture_t(uint32_t output_rate_hz, double clock_hz)
      : rate_(output_rate_hz), previous_clock_(g_current_clk_6502) {
    g_current_clk_6502 = clock_hz;
    audio_mixer_initialize(output_rate_hz);
  }

  ~MixerFixture_t() {
    audio_mixer_destroy();
    g_current_clk_6502 = previous_clock_;
  }

  MixerFixture_t(const MixerFixture_t&) = delete;
  auto operator=(const MixerFixture_t&) -> MixerFixture_t& = delete;
  MixerFixture_t(MixerFixture_t&&) = delete;
  auto operator=(MixerFixture_t&&) -> MixerFixture_t& = delete;

  auto rate() const -> uint32_t { return rate_; }

  auto set_clock(double clock_hz) -> void { g_current_clk_6502 = clock_hz; }

  auto upload_mono(int slot, const float* samples, size_t count) -> void {
    const float* channels[1] = {samples};
    audio_mixer_upload_channels(SOURCE_ID, slot, channels, 1,
                                static_cast<uint32_t>(count));
  }

  auto upload_planar(int slot, const std::vector<std::vector<float>>& planes,
                     size_t offset, size_t count) -> void {
    std::vector<const float*> channels;
    channels.reserve(planes.size());
    for (const auto& plane : planes) {
      channels.push_back(plane.data() + offset);
    }
    audio_mixer_upload_channels(SOURCE_ID, slot, channels.data(),
                                channels.size(), static_cast<uint32_t>(count));
  }

  // Interleaved stereo, two int16_t per frame.
  auto drain(size_t frames) -> std::vector<int16_t> {
    std::vector<int16_t> out(frames * 2, 0);
    audio_mixer_get_samples(out.data(), out.size());
    return out;
  }

  // Empties every ring and lets the underrun fade run to zero, so a
  // measurement on one slot cannot inherit another slot's residue.
  auto settle() -> void {
    drain(2048);
    drain(2048);
  }

  /**
   * @brief How many output frames a source of `count` samples produces.
   *
   * The ring holds a fraction of a second, so a second's worth can only be
   * counted by emptying the ring as it fills. A full-scale constant makes the
   * two kinds of frame distinguishable: a produced frame carries the level
   * exactly, and the underrun fade steps away from it immediately.
   */
  auto produced_frames(int slot, double source_rate_hz, size_t count,
                       size_t num_channels = 1) -> size_t {
    const size_t chunk_in = chunk_for(source_rate_hz);
    const std::vector<std::vector<float>> planes(
        num_channels, std::vector<float>(chunk_in, 1.0f));

    size_t produced = 0;
    size_t uploaded = 0;
    while (uploaded < count) {
      const size_t take = std::min(chunk_in, count - uploaded);
      upload_planar(slot, planes, 0, take);
      uploaded += take;
      produced += count_full_scale(drain(2 * frames_per_chunk));
    }
    produced += count_full_scale(drain(2 * frames_per_chunk));
    return produced;
  }

  /**
   * @brief The produced output frames of a source, with no fade in them.
   *
   * Each drain stops two frames short of what its chunk produced, so the
   * leftover stays in the ring and the result is a contiguous prefix of the
   * real output. Draining exactly what was produced is not possible from
   * outside the mixer, and draining more admits the fade.
   */
  auto stream(int slot, double source_rate_hz, const std::vector<float>& source)
      -> std::vector<int16_t> {
    const double ratio = static_cast<double>(rate_) / source_rate_hz;
    const size_t chunk_in = chunk_for(source_rate_hz);
    const size_t chunk_out =
        static_cast<size_t>(std::floor(static_cast<double>(chunk_in) * ratio));
    const size_t drain_frames = chunk_out - 2;

    std::vector<int16_t> out;
    size_t offset = 0;
    while (offset + chunk_in <= source.size()) {
      upload_mono(slot, source.data() + offset, chunk_in);
      offset += chunk_in;
      const std::vector<int16_t> block = drain(drain_frames);
      out.insert(out.end(), block.begin(), block.end());
    }
    return out;
  }

 private:
  static constexpr size_t frames_per_chunk = 1000;

  auto chunk_for(double source_rate_hz) const -> size_t {
    const double ratio = static_cast<double>(rate_) / source_rate_hz;
    return std::max<size_t>(
        1, static_cast<size_t>(
               std::ceil(static_cast<double>(frames_per_chunk) / ratio)));
  }

  static auto count_full_scale(const std::vector<int16_t>& block) -> size_t {
    size_t count = 0;
    for (size_t i = 0; i < block.size(); i += 2) {
      count += static_cast<size_t>(block[i] == 32767);
    }
    return count;
  }

  uint32_t rate_;
  double previous_clock_;
};

auto count_zero_crossings(const std::vector<int16_t>& stereo, size_t frames)
    -> size_t {
  size_t crossings = 0;
  for (size_t i = 0; i + 1 < frames; ++i) {
    const bool was_negative = stereo[i * 2] < 0;
    const bool is_negative = stereo[(i + 1) * 2] < 0;
    crossings += static_cast<size_t>(was_negative != is_negative);
  }
  return crossings;
}

// --- The channel tap recorder ---

std::vector<float> g_tapped;
size_t g_tap_calls = 0;

auto record_tap(const char* peripheral_id, int slot,
                const float* const* channels, size_t num_channels,
                size_t num_samples) -> void {
  (void)peripheral_id;
  (void)slot;
  g_tap_calls++;
  for (size_t c = 0; c < num_channels; ++c) {
    g_tapped.insert(g_tapped.end(), channels[c], channels[c] + num_samples);
  }
}

class ScopedTap_t {
 public:
  ScopedTap_t() {
    g_tapped.clear();
    g_tap_calls = 0;
    audio_mixer_set_channel_tap_callback(record_tap);
  }

  ~ScopedTap_t() {
    audio_mixer_set_channel_tap_callback(nullptr);
    g_tapped.clear();
    g_tap_calls = 0;
  }

  ScopedTap_t(const ScopedTap_t&) = delete;
  auto operator=(const ScopedTap_t&) -> ScopedTap_t& = delete;
  ScopedTap_t(ScopedTap_t&&) = delete;
  auto operator=(ScopedTap_t&&) -> ScopedTap_t& = delete;

  auto calls() const -> size_t { return g_tap_calls; }
  auto samples() const -> const std::vector<float>& { return g_tapped; }
};

}  // namespace

// =============================================================================
// The two-form time base at the ABI (Task A1)
// =============================================================================

TEST_CASE("Audio Mixer: Both Time Bases Resolve To The Same Second") {
  // A CPU-clocked source with any divisor and an absolute-rate source are two
  // spellings of a rate, and a second of either is a second of output. This
  // is the only case that exercises the divisor and the zero rule.
  constexpr uint32_t output_rate = 50000;
  constexpr double clock_hz = 1000000.0;
  MixerFixture_t mixer(output_rate, clock_hz);

  const PeripheralAudioInfo_t cpu_one = cpu_clocked_info(1, 1, 1.0f);
  const PeripheralAudioInfo_t cpu_sixteen = cpu_clocked_info(16, 1, 1.0f);
  const PeripheralAudioInfo_t absolute = absolute_info(44100, 1, 1.0f);
  PeripheralAudioInfo_t zeroed{};
  std::memset(&zeroed, 0, sizeof(zeroed));

  audio_mixer_register_source(0, SOURCE_ID, &cpu_one);
  audio_mixer_register_source(1, SOURCE_ID, &cpu_sixteen);
  audio_mixer_register_source(2, SOURCE_ID, &absolute);
  audio_mixer_register_source(3, SOURCE_ID, &zeroed);

  mixer.settle();
  CHECK(mixer.produced_frames(0, clock_hz, 1000000) == output_rate);

  mixer.settle();
  CHECK(mixer.produced_frames(1, clock_hz / 16.0, 62500) == output_rate);

  mixer.settle();
  CHECK(mixer.produced_frames(2, 44100.0, 44100) == output_rate);

  // A source that cannot say what its samples mean in time was never
  // registered, so its upload has nowhere to go.
  mixer.settle();
  const std::vector<float> ones(4096, 1.0f);
  mixer.upload_mono(3, ones.data(), ones.size());
  const std::vector<int16_t> silence = mixer.drain(2048);
  for (int16_t sample : silence) {
    CHECK(sample == 0);
  }
}

TEST_CASE("Audio Mixer: A CPU-Clocked Source Follows The Video Standard") {
  // The subject that used to live in the speaker: the same frame of samples
  // is a different length of time on an NTSC machine and a PAL one, and only
  // the mixer is in a position to know which.
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);

  const PeripheralAudioInfo_t info = cpu_clocked_info(1, 1, 1.0f);
  audio_mixer_register_source(0, SOURCE_ID, &info);

  const std::vector<float> frame(NTSC_FRAME_CYCLES, 1.0f);
  mixer.upload_mono(0, frame.data(), frame.size());
  std::vector<int16_t> out = mixer.drain(2048);
  size_t produced = 0;
  for (size_t i = 0; i < out.size(); i += 2) {
    produced += static_cast<size_t>(out[i] == 32767);
  }
  // 17030 * 48000 / 1020484 = 801.03
  CHECK(produced >= 800);
  CHECK(produced <= 802);

  mixer.settle();
  mixer.set_clock(CLOCK_6502_PAL);
  audio_mixer_register_source(0, SOURCE_ID, &info);
  mixer.upload_mono(0, frame.data(), frame.size());
  out = mixer.drain(2048);
  produced = 0;
  for (size_t i = 0; i < out.size(); i += 2) {
    produced += static_cast<size_t>(out[i] == 32767);
  }
  // 17030 * 48000 / 1015625 = 804.86
  CHECK(produced >= 804);
  CHECK(produced <= 806);
}

// =============================================================================
// The resampler in both directions (Task C1 step 6)
// =============================================================================

TEST_CASE("Audio Mixer: A 1 kHz Tone Keeps Its Pitch At Every Device Rate") {
  // The ratios here run from 47:1 down to below 1:1 and are fractional in
  // every case. A resampler that dropped samples or used an integer window
  // would still put out a tone; one that resolved the source's rate wrongly
  // would put out a different note.
  for (uint32_t rate : {22050U, 48000U, 192000U}) {
    for (int source_is_cpu_clocked : {0, 1}) {
      MixerFixture_t mixer(rate, CLOCK_6502_NTSC);
      const double source_rate =
          (source_is_cpu_clocked != 0) ? CLOCK_6502_NTSC : 44100.0;
      const PeripheralAudioInfo_t info = (source_is_cpu_clocked != 0)
                                             ? cpu_clocked_info(1, 1, 1.0f)
                                             : absolute_info(44100, 1, 1.0f);
      audio_mixer_register_source(0, SOURCE_ID, &info);

      // A tenth over one second, so that the counted window of exactly
      // output_rate frames stays inside the produced span.
      const size_t source_samples =
          static_cast<size_t>(std::ceil(source_rate * 1.1));
      const std::vector<float> tone =
          square_wave(source_rate, 1000.0, source_samples);

      const std::vector<int16_t> out = mixer.stream(0, source_rate, tone);
      REQUIRE(out.size() / 2 > rate);

      const size_t crossings = count_zero_crossings(out, rate);
      CHECK(crossings >= 1999);
      CHECK(crossings <= 2001);

      // Nothing in the chain exceeds full scale and the plateaus reach it
      // exactly: the source peaks at 1.0 and its gain is 1.0.
      int16_t highest = 0;
      int16_t lowest = 0;
      for (size_t i = 0; i < rate * 2; i += 2) {
        highest = std::max(highest, out[i]);
        lowest = std::min(lowest, out[i]);
      }
      CHECK(highest == 32767);
      CHECK(lowest == -32767);
    }
  }
}

TEST_CASE("Audio Mixer: A Source At The Device Rate Passes Through Untouched") {
  // Identity is the one ratio where the resampler must be invisible: sample
  // for sample, count exact, with only the single conversion applied.
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);

  const PeripheralAudioInfo_t info = absolute_info(output_rate, 1, 1.0f);
  audio_mixer_register_source(0, SOURCE_ID, &info);

  constexpr size_t count = 4096;
  std::vector<float> source(count);
  for (size_t i = 0; i < count; ++i) {
    source[i] = (static_cast<float>(i % 2001) / 1000.0f) - 1.0f;
  }

  mixer.upload_mono(0, source.data(), count);
  const std::vector<int16_t> out = mixer.drain(count);

  for (size_t i = 0; i < count; ++i) {
    const auto expected =
        static_cast<int16_t>(std::lroundf(source[i] * 32767.0f));
    CHECK(out[i * 2] == expected);
    CHECK(out[(i * 2) + 1] == expected);
  }
}

TEST_CASE("Audio Mixer: Frame Counts Follow The Ratio In Both Directions") {
  // floor(S * out / in), give or take the frame the fractional window is
  // holding when the upload ends.
  for (uint32_t rate : {22050U, 48000U, 192000U}) {
    for (int source_is_cpu_clocked : {0, 1}) {
      MixerFixture_t mixer(rate, CLOCK_6502_NTSC);
      const double source_rate =
          (source_is_cpu_clocked != 0) ? CLOCK_6502_NTSC : 44100.0;
      const PeripheralAudioInfo_t info = (source_is_cpu_clocked != 0)
                                             ? cpu_clocked_info(1, 1, 1.0f)
                                             : absolute_info(44100, 1, 1.0f);
      audio_mixer_register_source(0, SOURCE_ID, &info);

      const auto source_samples = static_cast<size_t>(source_rate / 10.0);
      const auto expected = static_cast<size_t>(
          std::floor(static_cast<double>(source_samples) *
                     static_cast<double>(rate) / source_rate));
      const size_t produced =
          mixer.produced_frames(0, source_rate, source_samples);
      CHECK(produced >= expected - 1);
      CHECK(produced <= expected + 1);
    }
  }
}

// =============================================================================
// Channel handling
// =============================================================================

TEST_CASE("Audio Mixer: Every Channel Of A Source Advances Together") {
  // The mixer takes the produced frame count from the last channel and
  // interleaves by index, which is only correct because every channel's
  // resampler advances identically. A six-channel source split three to the
  // left and three to the right must therefore put out two identical sides.
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);

  const PeripheralAudioInfo_t six = absolute_info(44100, 6, 1.0f);
  const PeripheralAudioInfo_t one = absolute_info(44100, 1, 1.0f);
  audio_mixer_register_source(1, SOURCE_ID, &six);
  audio_mixer_register_source(2, SOURCE_ID, &one);

  // Only channel 0 reaches an output, so the count is that channel's alone.
  for (size_t c = 0; c < 6; ++c) {
    audio_mixer_set_channel_pan(1, c, (c == 0) ? 1.0f : 0.0f, 0.0f);
  }
  audio_mixer_set_channel_pan(2, 0, 1.0f, 0.0f);

  mixer.settle();
  const size_t six_channel = mixer.produced_frames(1, 44100.0, 4410, 6);
  mixer.settle();
  const size_t one_channel = mixer.produced_frames(2, 44100.0, 4410, 1);
  CHECK(six_channel == one_channel);

  // Three channels to each side, a third of the signal each: the two sides
  // are the same samples reached by different indices.
  audio_mixer_unregister_source(2);
  audio_mixer_register_source(1, SOURCE_ID, &six);
  for (size_t c = 0; c < 3; ++c) {
    audio_mixer_set_channel_pan(1, c, 1.0f / 3.0f, 0.0f);
    audio_mixer_set_channel_pan(1, c + 3, 0.0f, 1.0f / 3.0f);
  }
  mixer.settle();

  constexpr size_t source_samples = 4410;
  const std::vector<std::vector<float>> planes(
      6, square_wave(44100.0, 1000.0, source_samples));
  mixer.upload_planar(1, planes, 0, source_samples);

  const std::vector<int16_t> out = mixer.drain(4000);
  for (size_t i = 0; i < 4000; ++i) {
    CHECK(out[i * 2] == out[(i * 2) + 1]);
  }
  CHECK(out[0] == 32767);
}

// =============================================================================
// The single clip, the rounding, and the per-source gain (Tasks A2, C1)
// =============================================================================

TEST_CASE("Audio Mixer: One Clip, At The Very End") {
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);
  ScopedTap_t tap;

  const PeripheralAudioInfo_t info = absolute_info(output_rate, 1, 1.0f);
  audio_mixer_register_source(0, SOURCE_ID, &info);
  audio_mixer_register_source(1, SOURCE_ID, &info);

  // The tap sits ahead of gain, pan, resampling and the clip on purpose: it
  // reports what the peripheral emitted, not what came out.
  audio_mixer_set_channel_pan(0, 0, 0.5f, 0.5f);
  const std::vector<float> hot(64, 1.6f);
  mixer.upload_mono(0, hot.data(), hot.size());
  REQUIRE(tap.calls() == 1);
  REQUIRE(tap.samples().size() == hot.size());
  CHECK(tap.samples()[0] == 1.6f);

  std::vector<int16_t> out = mixer.drain(32);
  // lroundf(1.6 * 0.5 * 32767). A clip applied before the pan would have
  // given 16384 instead.
  CHECK(out[0] == 26214);

  auto drain_first = [&mixer](float left, float right) -> std::pair<int, int> {
    const std::vector<float> a(64, left);
    const std::vector<float> b(64, right);
    mixer.upload_mono(0, a.data(), a.size());
    mixer.upload_mono(1, b.data(), b.size());
    const std::vector<int16_t> block = mixer.drain(32);
    return {block[0], block[1]};
  };

  mixer.settle();
  audio_mixer_reset_channel_pan(0);

  // Two sources summing past full scale clip once, at the rails.
  CHECK(drain_first(0.75f, 0.75f).first == 32767);
  mixer.settle();
  CHECK(drain_first(-0.75f, -0.75f).first == -32767);

  // And the pinned conversions, with the second source silent.
  mixer.settle();
  CHECK(drain_first(0.5f, 0.0f).first == 16384);
  mixer.settle();
  CHECK(drain_first(1.0f, 0.0f).first == 32767);
  mixer.settle();
  CHECK(drain_first(-1.0f, 0.0f).first == -32767);
}

TEST_CASE("Audio Mixer: A Source's Peak Sets Its Default Gain") {
  // The speaker's 2.0 onset edge would clip at the rail on every note if the
  // mixer did not attenuate by 1 / peak_magnitude, and 0.5 is exactly the
  // old 16384-times-2 full-scale peak.
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);
  ScopedTap_t tap;

  const PeripheralAudioInfo_t info = absolute_info(output_rate, 1, 2.0f);
  audio_mixer_register_source(0, SOURCE_ID, &info);

  const std::vector<float> edge(64, 2.0f);
  mixer.upload_mono(0, edge.data(), edge.size());
  CHECK(mixer.drain(32)[0] == 32767);

  // The steady plateau of a 1 kHz tone, 2 / (1 + a^510).
  mixer.settle();
  const std::vector<float> plateau(64, 1.01109f);
  mixer.upload_mono(0, plateau.data(), plateau.size());
  const int16_t at_plateau = mixer.drain(32)[0];
  CHECK(at_plateau >= 16564);
  CHECK(at_plateau <= 16566);

  // Overriding the gain is what makes the clip engage, and the tap still
  // reports the unclipped 2.0 the peripheral emitted.
  mixer.settle();
  audio_mixer_set_source_gain(0, 1.0f);
  mixer.upload_mono(0, edge.data(), edge.size());
  CHECK(mixer.drain(32)[0] == 32767);
  CHECK(tap.samples().back() == 2.0f);
}

// =============================================================================
// Idempotent re-announcement (Task A3)
// =============================================================================

TEST_CASE("Audio Mixer: An Unchanged Re-Announcement Changes Nothing") {
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);

  const PeripheralAudioInfo_t info = absolute_info(output_rate, 1, 1.0f);
  audio_mixer_register_source(1, SOURCE_ID, &info);
  audio_mixer_set_channel_pan(1, 0, 0.3f, 0.7f);

  constexpr size_t frames = 256;
  const std::vector<float> payload(frames, 0.5f);
  mixer.upload_mono(1, payload.data(), payload.size());

  audio_mixer_register_source(1, SOURCE_ID, &info);

  float left = 0.0f;
  float right = 0.0f;
  audio_mixer_get_channel_pan(1, 0, &left, &right);
  CHECK(left == 0.3f);
  CHECK(right == 0.7f);

  // The payload survived, so the ring was neither reinitialized nor flushed,
  // and the pan override survived, so the defaults were not re-applied.
  const std::vector<int16_t> out = mixer.drain(512);
  for (size_t i = 0; i < frames; ++i) {
    CHECK(out[i * 2] == 4915);
    CHECK(out[(i * 2) + 1] == 11468);
  }

  // A genuine layout change is a different matter: pan assignments made
  // against the old channel count are meaningless, and the backlog was
  // resampled for a layout that no longer exists.
  mixer.settle();
  mixer.upload_mono(1, payload.data(), payload.size());
  const PeripheralAudioInfo_t wider = absolute_info(output_rate, 2, 1.0f);
  audio_mixer_register_source(1, SOURCE_ID, &wider);

  audio_mixer_get_channel_pan(1, 0, &left, &right);
  CHECK(left == 1.0f);
  CHECK(right == 1.0f);

  const std::vector<int16_t> after = mixer.drain(512);
  for (size_t i = 0; i < frames; ++i) {
    CHECK(after[i * 2] == 0);
    CHECK(after[(i * 2) + 1] == 0);
  }
}

// =============================================================================
// Latency in milliseconds, and the underrun contract (Task C1 step 4)
// =============================================================================

TEST_CASE("Audio Mixer: The Backlog Skip Is Measured In Milliseconds") {
  // A ramp makes the discarded prefix readable: out[0] names the frame the
  // skip stopped at.
  //
  // Two corrections to the plan's fixture row, which gives
  // (200 - 10 - 140) * rate / 1000. First, the mixer counts its capacity and
  // cushion in interleaved samples rather than frames -- which is how
  // today's 16384 and 6144 at 44100 are preserved -- so the 140 costs half
  // that many milliseconds of frames. Second, 370 ms of capacity is 185 ms
  // of frames, so a 200 ms upload saturates the ring and the measurement
  // becomes capacity-limited rather than cushion-limited. 150 ms fits.
  constexpr size_t upload_ms = 150;
  for (uint32_t rate : {48000U, 96000U}) {
    MixerFixture_t mixer(rate, CLOCK_6502_NTSC);
    const PeripheralAudioInfo_t info = absolute_info(rate, 1, 1.0f);
    audio_mixer_register_source(0, SOURCE_ID, &info);

    const size_t upload_frames = rate * upload_ms / 1000;
    const size_t drain_frames = rate * 10 / 1000;
    const size_t cushion_samples = rate * MIXER_CUSHION_MS / 1000;
    REQUIRE(upload_frames * 2 < (rate * MIXER_CAPACITY_MS / 1000));

    std::vector<float> ramp(upload_frames);
    for (size_t i = 0; i < upload_frames; ++i) {
      ramp[i] = static_cast<float>(i) / 32767.0f;
    }
    mixer.upload_mono(0, ramp.data(), ramp.size());

    const size_t target_backlog = (drain_frames * 2) + cushion_samples;
    const size_t skipped = ((upload_frames * 2) - target_backlog) & ~size_t(1);
    const std::vector<int16_t> out = mixer.drain(drain_frames);
    CHECK(out[0] == static_cast<int16_t>(skipped / 2));
  }

  // The two values divided by their rates are the same number of
  // milliseconds; the literals are time, not samples.
  CHECK((48000 * upload_ms / 1000) - (48000 * 10 / 1000) -
            ((48000 * MIXER_CUSHION_MS / 1000) / 2) ==
        3360);
  CHECK((96000 * upload_ms / 1000) - (96000 * 10 / 1000) -
            ((96000 * MIXER_CUSHION_MS / 1000) / 2) ==
        6720);
}

TEST_CASE("Audio Mixer: The Ring's Capacity Is Measured In Milliseconds Too") {
  // More than the ring holds is dropped at the write end, so a saturating
  // upload leaves the ring two samples short of capacity: it keeps one sample
  // free and only ever writes an even count. The capacity itself is rounded
  // down to an even sample count because several length computations mask the
  // low bit, so at 44100 it is 16316 samples and not 16317.
  for (uint32_t rate : {44100U, 48000U}) {
    MixerFixture_t mixer(rate, CLOCK_6502_NTSC);
    const PeripheralAudioInfo_t info = absolute_info(rate, 1, 1.0f);
    audio_mixer_register_source(0, SOURCE_ID, &info);

    std::vector<float> ramp(rate);
    for (size_t i = 0; i < ramp.size(); ++i) {
      ramp[i] = static_cast<float>(i) / 32767.0f;
    }
    mixer.upload_mono(0, ramp.data(), ramp.size());

    const size_t capacity_samples =
        (rate * MIXER_CAPACITY_MS / 1000) & ~size_t(1);
    const size_t filled = capacity_samples - 2;
    const size_t drain_frames = rate * 10 / 1000;
    const size_t target_backlog =
        (drain_frames * 2) + (rate * MIXER_CUSHION_MS / 1000);
    const size_t skipped = (filled - target_backlog) & ~size_t(1);

    const std::vector<int16_t> out = mixer.drain(drain_frames);
    CHECK(out[0] == static_cast<int16_t>(skipped / 2));
  }

  CHECK(((44100 * MIXER_CAPACITY_MS / 1000) & ~size_t(1)) == 16316);
}

TEST_CASE("Audio Mixer: An Underrun Fades Rather Than Repeating") {
  // Every frontend already relies on this: when the emulation falls behind,
  // the last frame walks to zero at a fixed step instead of clicking or
  // repeating. The plan calls it a hold; the code has faded since before this
  // work, and the fade is what is pinned.
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);

  const PeripheralAudioInfo_t info = absolute_info(output_rate, 1, 1.0f);
  audio_mixer_register_source(0, SOURCE_ID, &info);

  const std::vector<float> payload(10, 1.0f);
  mixer.upload_mono(0, payload.data(), payload.size());

  const std::vector<int16_t> out = mixer.drain(20);
  for (size_t i = 0; i < 10; ++i) {
    CHECK(out[i * 2] == 32767);
  }
  for (size_t i = 10; i < 20; ++i) {
    const auto expected =
        static_cast<int16_t>(32767 - (FADE_STEP_PCM * static_cast<int>(i - 9)));
    CHECK(out[i * 2] == expected);
    CHECK(out[(i * 2) + 1] == expected);
  }
}

// =============================================================================
// Adversarial boundaries
// =============================================================================

TEST_CASE("Audio Mixer: More Than The Ring Holds Is Dropped At The Write End") {
  // The plan expects the most recent capacity frames to survive. They do not:
  // sample_buffer_upload writes what fits and drops the rest, so the ring
  // keeps the oldest frames and the producer loses the newest. Pinned as it
  // is, because the alternative -- overwriting the reader's backlog -- is not
  // something a single-producer single-consumer ring can do safely.
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);
  const PeripheralAudioInfo_t info = absolute_info(output_rate, 1, 1.0f);
  audio_mixer_register_source(0, SOURCE_ID, &info);

  const size_t capacity_samples =
      (output_rate * MIXER_CAPACITY_MS / 1000) & ~size_t(1);
  const size_t overflowing_frames = capacity_samples * 4;

  std::vector<float> ramp(overflowing_frames);
  for (size_t i = 0; i < overflowing_frames; ++i) {
    ramp[i] = static_cast<float>(i % 1000) / 1000.0f;
  }
  mixer.upload_mono(0, ramp.data(), ramp.size());

  // The ring keeps one sample free and only ever writes an even count, so a
  // saturating upload leaves it two samples short of capacity.
  const size_t retained = (capacity_samples - 2) / 2;
  const std::vector<int16_t> out = mixer.drain(capacity_samples);

  for (size_t i = 0; i < retained; ++i) {
    const auto expected =
        static_cast<int16_t>(std::lroundf(ramp[i] * 32767.0f));
    CHECK(out[i * 2] == expected);
  }

  // The frame after the retained prefix is the underrun fade, not the next
  // ramp value: the newest frames were dropped, not the oldest.
  const auto dropped =
      static_cast<int16_t>(std::lroundf(ramp[retained] * 32767.0f));
  CHECK(out[retained * 2] != dropped);

  for (int16_t sample : out) {
    CHECK(sample >= -32767);
    CHECK(sample <= 32767);
  }
}

TEST_CASE("Audio Mixer: Malformed Uploads Are Refused") {
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);
  const PeripheralAudioInfo_t info = absolute_info(output_rate, 1, 1.0f);
  audio_mixer_register_source(0, SOURCE_ID, &info);

  const std::vector<float> payload(64, 1.0f);
  const float* one[1] = {payload.data()};
  const float* null_inside[2] = {payload.data(), nullptr};

  // A channel count of zero or beyond the per-slot maximum, a null plane, a
  // null plane array, and an empty upload all leave the ring untouched.
  audio_mixer_upload_channels(SOURCE_ID, 0, one, 0, 64);
  audio_mixer_upload_channels(SOURCE_ID, 0, one, 17, 64);
  audio_mixer_upload_channels(SOURCE_ID, 0, null_inside, 2, 64);
  audio_mixer_upload_channels(SOURCE_ID, 0, nullptr, 1, 64);
  audio_mixer_upload_channels(SOURCE_ID, 0, one, 1, 0);

  const std::vector<int16_t> out = mixer.drain(64);
  for (int16_t sample : out) {
    CHECK(sample == 0);
  }

  // A well-formed upload on the same slot still works, so nothing was left
  // in a broken state.
  mixer.upload_mono(0, payload.data(), payload.size());
  CHECK(mixer.drain(32)[0] == 32767);
}

TEST_CASE("Audio Mixer: Slots Outside The Range Are Refused") {
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);
  const PeripheralAudioInfo_t info = absolute_info(output_rate, 1, 1.0f);

  audio_mixer_register_source(-1, SOURCE_ID, &info);
  audio_mixer_register_source(8, SOURCE_ID, &info);
  audio_mixer_register_source(0, SOURCE_ID, nullptr);
  audio_mixer_unregister_source(-1);
  audio_mixer_unregister_source(8);

  const std::vector<float> payload(64, 1.0f);
  mixer.upload_mono(-1, payload.data(), payload.size());
  mixer.upload_mono(8, payload.data(), payload.size());

  audio_mixer_set_channel_pan(-1, 0, 1.0f, 1.0f);
  audio_mixer_set_channel_pan(8, 0, 1.0f, 1.0f);
  audio_mixer_set_channel_pan(0, 16, 1.0f, 1.0f);
  audio_mixer_set_source_gain(-1, 1.0f);
  audio_mixer_set_source_gain(8, 1.0f);
  audio_mixer_reset_channel_pan(-1);
  audio_mixer_reset_channel_pan(8);

  float left = -1.0f;
  float right = -1.0f;
  audio_mixer_get_channel_pan(-1, 0, &left, &right);
  CHECK(left == -1.0f);
  CHECK(right == -1.0f);
  audio_mixer_get_channel_pan(0, 0, nullptr, nullptr);

  const std::vector<int16_t> out = mixer.drain(64);
  for (int16_t sample : out) {
    CHECK(sample == 0);
  }
}

TEST_CASE("Audio Mixer: An Out-Of-Range Pan Is Stored, Not Clamped") {
  // Pinning which: the mixer keeps whatever pan it is given, and the only
  // limit in the chain is the single conversion at the end. A frontend that
  // wants a bounded control has to bound it itself.
  constexpr uint32_t output_rate = 48000;
  MixerFixture_t mixer(output_rate, CLOCK_6502_NTSC);
  const PeripheralAudioInfo_t info = absolute_info(output_rate, 1, 1.0f);
  audio_mixer_register_source(0, SOURCE_ID, &info);

  audio_mixer_set_channel_pan(0, 0, -1.0f, 2.0f);
  float left = 0.0f;
  float right = 0.0f;
  audio_mixer_get_channel_pan(0, 0, &left, &right);
  CHECK(left == -1.0f);
  CHECK(right == 2.0f);

  const std::vector<float> payload(64, 0.5f);
  mixer.upload_mono(0, payload.data(), payload.size());
  const std::vector<int16_t> out = mixer.drain(32);
  CHECK(out[0] == -16384);
  CHECK(out[1] == 32767);
}

TEST_CASE("Audio Mixer: Draining Without A Mixer Is Silence") {
  // The audio thread can outlive the device. A drain before initialize, or
  // after destroy, writes silence rather than reading a freed ring.
  audio_mixer_destroy();

  std::vector<int16_t> out(256, 0x5A5A);
  audio_mixer_get_samples(out.data(), out.size());
  for (int16_t sample : out) {
    CHECK(sample == 0);
  }

  // And the degenerate requests do nothing at all.
  audio_mixer_get_samples(nullptr, 256);
  audio_mixer_get_samples(out.data(), 0);
  audio_mixer_clear_buffers();
  audio_mixer_set_fade(fade_out);
  audio_mixer_set_fade(fade_in);
}

TEST_CASE("Audio Mixer: A Zero Output Rate Produces No Output") {
  // A device that reports no rate at all cannot be resampled to, so the
  // mixer keeps nothing and plays nothing rather than dividing by it.
  MixerFixture_t mixer(0, CLOCK_6502_NTSC);
  const PeripheralAudioInfo_t info = absolute_info(48000, 1, 1.0f);
  audio_mixer_register_source(0, SOURCE_ID, &info);

  const std::vector<float> payload(64, 1.0f);
  mixer.upload_mono(0, payload.data(), payload.size());

  std::vector<int16_t> out(128, 0x5A5A);
  audio_mixer_get_samples(out.data(), out.size());
  for (int16_t sample : out) {
    CHECK(sample == 0);
  }
}
