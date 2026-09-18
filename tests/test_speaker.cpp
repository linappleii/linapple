// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "doctest.h"

namespace {

constexpr uint16_t ADDR_SPEAKER = 0xC030;
constexpr int TEST_SLOT = 0;
constexpr uint64_t NTSC_ONE_SECOND_CYCLES = 1022727;
constexpr uint32_t NTSC_FRAME_CYCLES = 17030;

// The cone model, from the peripheral's own literals. Every expected value
// below is derived from these and never from running the code.
constexpr double EDGE = 2.0;
constexpr double TAU_CYCLES = 23000.0;
constexpr double FILTER_A = 1.0 - (1.0 / TAU_CYCLES);
constexpr float SILENCE_EPSILON = 0.001f;

// ln(EDGE / epsilon) / -ln(a) is 174,817 cycles from an edge down to the
// silence epsilon: ten NTSC frames and part of an eleventh, so eleven frames
// of thinking carry audio.
constexpr int FRAMES_TO_SILENCE = 11;

// The per-update capacity limits from Speaker.cpp.
constexpr uint32_t SPEAKER_MAX_SAMPLES_PER_UPDATE = 24000;
constexpr size_t SPEAKER_MAX_EVENTS_PER_UPDATE = 8192;

// Every edge is a step of exactly 2.0 through the DC blocker, emitted as
// normalized float with nothing in the peripheral limiting it.
constexpr float EDGE_POSITIVE = 2.0f;
constexpr float EDGE_NEGATIVE = -2.0f;

struct MockDirectIOHandler_t {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockDirectIOHandler_t() = default;
  MockDirectIOHandler_t(void* inst, PeripheralIOHandler r,
                        PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

struct MockStrobeHandler_t {
  void* instance = nullptr;
  PeripheralStrobeHandler_t on_strobe = nullptr;

  MockStrobeHandler_t() = default;
  MockStrobeHandler_t(void* inst, PeripheralStrobeHandler_t handler)
      : instance(inst), on_strobe(handler) {}
};

struct SpeakerHarness_t {
  SpeakerHarness_t() {
    assert(s_active_harness == nullptr);
    s_active_harness = this;
    host_.Log = mock_log;
    host_.AssertIrq = mock_assert_irq;
    host_.RegisterIO = nullptr;
    host_.RegisterCxROM = nullptr;
    host_.RegisterExpansionROM = nullptr;
    host_.RegisterDirectIO = mock_register_direct_io;
    host_.RegisterDirectIOStrobe = mock_register_direct_io_strobe;
    host_.AudioPushChannels = mock_audio_push_channels;
    host_.GetCycles = mock_get_cycles;
  }

  ~SpeakerHarness_t() {
    for (void* inst : instances_) {
      if (inst != nullptr) {
        speaker_get_descriptor()->shutdown(inst);
      }
    }
    instances_.clear();
    primary_instance_ = nullptr;
    handlers_.clear();
    strobes_.clear();
    captured_samples_.clear();
    captured_channels_ = 0;
    s_active_harness = nullptr;
  }

  SpeakerHarness_t(const SpeakerHarness_t&) = delete;
  auto operator=(const SpeakerHarness_t&) -> SpeakerHarness_t& = delete;
  SpeakerHarness_t(SpeakerHarness_t&&) = delete;
  auto operator=(SpeakerHarness_t&&) -> SpeakerHarness_t& = delete;

  auto host() -> HostInterface_t* { return &host_; }

  auto create_speaker(int slot = TEST_SLOT) -> void* {
    void* instance = speaker_get_descriptor()->init(slot, &host_);
    if (instance != nullptr) {
      instances_.push_back(instance);
      if (primary_instance_ == nullptr) {
        primary_instance_ = instance;
      }
    }
    return instance;
  }

  auto shutdown_instance(void* inst) -> void {
    if (inst == nullptr) {
      return;
    }
    auto it = std::find(instances_.begin(), instances_.end(), inst);
    if (it != instances_.end()) {
      instances_.erase(it);
    }
    if (primary_instance_ == inst) {
      primary_instance_ = instances_.empty() ? nullptr : instances_.front();
    }
    speaker_get_descriptor()->shutdown(inst);
  }

  auto primary_instance() const -> void* { return primary_instance_; }

  auto cycles() const -> uint64_t { return cycles_; }
  auto set_cycles(uint64_t c) -> void { cycles_ = c; }
  auto advance_cycles(uint64_t delta) -> void { cycles_ += delta; }

  auto captured_channels() const -> size_t { return captured_channels_; }

  auto set_drop_audio(bool drop) -> void {
    host_.AudioPushChannels = drop ? nullptr : mock_audio_push_channels;
  }

  auto set_null_cycles(bool null_cycles) -> void {
    host_.GetCycles = null_cycles ? nullptr : mock_get_cycles;
  }

  auto set_null_strobe_registration(bool is_null) -> void {
    host_.RegisterDirectIOStrobe =
        is_null ? nullptr : mock_register_direct_io_strobe;
  }

  // The legacy bus-driving registration must stay unused: a speaker that
  // reaches for it has regressed to reading the floating bus itself.
  auto legacy_handlers_empty() const -> bool { return handlers_.empty(); }

  auto strobe_registered(uint16_t addr) const -> bool {
    return strobes_.find(addr) != strobes_.end();
  }

  auto strobe_registration_count() const -> size_t { return strobes_.size(); }

  auto strobe(void* inst = nullptr) -> void {
    auto it = strobes_.find(ADDR_SPEAKER);
    if (it == strobes_.end() || it->second.on_strobe == nullptr) {
      return;
    }
    void* target = (inst != nullptr)                  ? inst
                   : (it->second.instance != nullptr) ? it->second.instance
                                                      : primary_instance_;
    it->second.on_strobe(target);
  }

  // Deliver a strobe to a verbatim instance pointer, nullptr included.
  auto strobe_instance(void* inst) -> void {
    auto it = strobes_.find(ADDR_SPEAKER);
    if (it != strobes_.end() && it->second.on_strobe != nullptr) {
      it->second.on_strobe(inst);
    }
  }

  auto think(void* inst, uint32_t elapsed_cycles) -> void {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (target != nullptr) {
      speaker_get_descriptor()->think(target, elapsed_cycles);
    }
  }

  auto captured_samples() const -> const std::vector<float>& {
    return captured_samples_;
  }
  auto audio_push_count() const -> uint32_t { return audio_push_count_; }
  auto clear_captured_samples() -> void {
    captured_samples_.clear();
    audio_push_count_ = 0;
  }

 private:
  HostInterface_t host_{};
  uint64_t cycles_ = 0;
  std::vector<void*> instances_;
  void* primary_instance_ = nullptr;
  std::map<uint16_t, MockDirectIOHandler_t> handlers_;
  std::map<uint16_t, MockStrobeHandler_t> strobes_;
  std::vector<float> captured_samples_;
  size_t captured_channels_ = 0;
  uint32_t audio_push_count_ = 0;

  static SpeakerHarness_t* s_active_harness;

  static auto mock_log(void* instance, PeripheralLogLevel_t level,
                       const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    (void)fmt;
  }

  static auto mock_assert_irq(int slot, bool assert) -> void {
    (void)slot;
    (void)assert;
  }

  static auto mock_register_direct_io(void* instance, uint16_t addr,
                                      PeripheralIOHandler read,
                                      PeripheralIOHandler write) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->handlers_[addr] =
          MockDirectIOHandler_t(instance, read, write);
    }
  }

  static auto mock_register_direct_io_strobe(
      void* instance, uint16_t addr, PeripheralStrobeHandler_t on_strobe)
      -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->strobes_[addr] =
          MockStrobeHandler_t(instance, on_strobe);
    }
  }

  static auto mock_audio_push_channels(void* instance,
                                       const float* const* channel_buffers,
                                       size_t num_channels, size_t num_samples)
      -> void {
    (void)instance;
    if (s_active_harness != nullptr && channel_buffers != nullptr &&
        num_channels > 0 && num_samples > 0) {
      s_active_harness->captured_channels_ = num_channels;
      if (channel_buffers[0] != nullptr) {
        s_active_harness->captured_samples_.insert(
            s_active_harness->captured_samples_.end(), channel_buffers[0],
            channel_buffers[0] + num_samples);
      }
      s_active_harness->audio_push_count_++;
    }
  }

  static auto mock_get_cycles() -> uint64_t {
    if (s_active_harness != nullptr) {
      return s_active_harness->cycles_;
    }
    return 0;
  }
};

SpeakerHarness_t* SpeakerHarness_t::s_active_harness = nullptr;

}  // namespace

// =============================================================================
// Domain 1: Registration, Descriptor & Multi-Card Lifecycle
// =============================================================================

TEST_CASE("Speaker Peripheral: Identity Descriptor Validation") {
  // TC-01: Identity Descriptor Validation
  auto* descriptor = speaker_get_descriptor();
  REQUIRE(descriptor != nullptr);

  CHECK(descriptor->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(descriptor->id) == "linapple.speaker");
  CHECK(std::string(descriptor->name) == "Speaker");
  // Nothing else in the tree asserts the description, and $C020 cassette
  // output is a separate flip-flop that this peripheral does not model.
  CHECK(std::string(descriptor->description) == "Built-in Apple II speaker");
  CHECK(descriptor->compatible_slots == PERIPHERAL_MASK_INTERNAL);
  CHECK(descriptor->default_slot == 0);
  CHECK(descriptor->init != nullptr);
  CHECK(descriptor->shutdown != nullptr);
  CHECK(descriptor->reset != nullptr);
  CHECK(descriptor->think != nullptr);
  CHECK(descriptor->save_state != nullptr);
  CHECK(descriptor->load_state != nullptr);
  CHECK(descriptor->query != nullptr);
  CHECK(descriptor->command == nullptr);
  CHECK(descriptor->on_vblank == nullptr);
}

TEST_CASE("Speaker Peripheral: Multi-Instance Isolation & RAII Lifecycle") {
  // TC-02: Multi-Instance Isolation & RAII Lifecycle
  SpeakerHarness_t harness;
  void* instance1 = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance1 != nullptr);

  void* instance2 = harness.create_speaker(TEST_SLOT + 1);
  REQUIRE(instance2 != nullptr);
  CHECK(instance1 != instance2);

  harness.set_cycles(1000);
  harness.strobe(instance1);
  harness.advance_cycles(100);
  harness.think(instance1, 100);
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
  harness.clear_captured_samples();

  harness.think(instance2, 100);
  CHECK(harness.audio_push_count() == 0);
  CHECK(harness.captured_samples().empty());

  harness.strobe(instance2);
  harness.advance_cycles(100);
  harness.think(instance2, 100);
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
  harness.clear_captured_samples();

  // Deallocating instance1 leaves instance2 operational
  harness.shutdown_instance(instance1);
  harness.strobe(instance2);
  harness.advance_cycles(100);
  harness.think(instance2, 100);
  CHECK(harness.audio_push_count() == 1);
  harness.shutdown_instance(instance2);
}

TEST_CASE("Speaker Peripheral: Re-Init After Shutdown Starts From Rest") {
  // A shut-down instance leaves nothing behind: the replacement's first
  // strobe is a full edge from silence, not a continuation of the old cone.
  SpeakerHarness_t harness;
  void* first = harness.create_speaker(TEST_SLOT);
  REQUIRE(first != nullptr);

  harness.set_cycles(1000);
  harness.strobe(first);
  harness.advance_cycles(100);
  harness.think(first, 100);
  REQUIRE(harness.audio_push_count() == 1);
  const std::vector<float> from_first = harness.captured_samples();

  harness.shutdown_instance(first);
  harness.clear_captured_samples();

  void* second = harness.create_speaker(TEST_SLOT);
  REQUIRE(second != nullptr);
  harness.think(second, 100);
  CHECK(harness.audio_push_count() == 0);

  harness.strobe(second);
  harness.advance_cycles(100);
  harness.think(second, 100);
  REQUIRE(harness.audio_push_count() == 1);
  REQUIRE(harness.captured_samples().size() == from_first.size());
  for (size_t i = 0; i < from_first.size(); ++i) {
    CHECK(harness.captured_samples()[i] == from_first[i]);
  }
}

// =============================================================================
// Domain 2: Motherboard Direct I/O & Physical Strobe Mechanics
// =============================================================================

TEST_CASE("Speaker Peripheral: Strobe Registration At The Soft Switch") {
  // TC-03: the speaker registers a strobe at $C030 and nothing else
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  CHECK(harness.strobe_registered(ADDR_SPEAKER));
  CHECK(harness.legacy_handlers_empty());

  // Before any strobe the cone is at rest and nothing is pushed
  harness.set_cycles(1000);
  harness.advance_cycles(100);
  harness.think(instance, 100);
  CHECK(harness.audio_push_count() == 0);

  // A strobe at cycle c starts the slice at c: the hundred cycles that
  // followed it are a hundred samples, and the first of them is the edge.
  harness.strobe();
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples().size() == 100);
  CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
}

TEST_CASE("Speaker Peripheral: One Strobe Registration Serves Read And Write") {
  // Any access to $C030 toggles the flip-flop, so the speaker asks for a
  // single strobe entry rather than one per direction. That a read and a
  // write through the real bridge reach the same handler is asserted in
  // test_speaker_core.cpp, which is the only place the bridge is observable.
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  CHECK(harness.strobe_registration_count() == 1);
  CHECK(harness.strobe_registered(ADDR_SPEAKER));
  CHECK(harness.legacy_handlers_empty());
}

TEST_CASE("Speaker Peripheral: Repeated Strobes Keep The Cone Driven") {
  // TC-04: four strobes in one slice leave the peripheral driving audio
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  for (int i = 0; i < 4; ++i) {
    harness.strobe(instance);
    harness.advance_cycles(50);
  }
  harness.think(instance, 200);
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
}

TEST_CASE("Speaker Peripheral: Alternating Flip-Flop Polarity Verification") {
  // TC-05: successive strobes drive the cone in opposite directions, and each
  // edge from rest is a step of exactly 2.0. Letting the cone settle between
  // edges is what makes each step observable on its own.
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  auto settle = [&harness, instance]() -> void {
    for (int frame = 0; frame < FRAMES_TO_SILENCE; ++frame) {
      harness.advance_cycles(NTSC_FRAME_CYCLES);
      harness.think(instance, NTSC_FRAME_CYCLES);
    }
    harness.clear_captured_samples();
  };

  auto strobe_and_capture_edge = [&harness, instance]() -> float {
    harness.strobe(instance);
    harness.advance_cycles(100);
    harness.think(instance, 100);
    REQUIRE(harness.audio_push_count() == 1);
    return harness.captured_samples()[0];
  };

  harness.set_cycles(100);
  CHECK(strobe_and_capture_edge() == EDGE_POSITIVE);
  settle();
  CHECK(strobe_and_capture_edge() == EDGE_NEGATIVE);
  settle();
  CHECK(strobe_and_capture_edge() == EDGE_POSITIVE);
  settle();
  CHECK(strobe_and_capture_edge() == EDGE_NEGATIVE);
}

// =============================================================================
// Domain 4: Digital Signal Processing & Filter Mechanics
// =============================================================================

TEST_CASE("Speaker Peripheral: A Cone At Rest Pushes Nothing") {
  // TC-08: silence is no samples, not a stream of zeros
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  harness.think(instance, 1000);

  CHECK(harness.audio_push_count() == 0);
  CHECK(harness.captured_samples().empty());

  // Ten whole frames of nothing stay nothing: silence is a property of the
  // cone's state, so it does not decay into a stream of zeros over time.
  for (int frame = 0; frame < 10; ++frame) {
    harness.advance_cycles(NTSC_FRAME_CYCLES);
    harness.think(instance, NTSC_FRAME_CYCLES);
  }
  CHECK(harness.audio_push_count() == 0);
  CHECK(harness.captured_samples().empty());
}

TEST_CASE("Speaker Peripheral: A Zero-Cycle Think Pushes Nothing") {
  // A slice of no cycles is no samples even with the cone in full swing.
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  harness.strobe();
  harness.think(instance, 0);

  CHECK(harness.audio_push_count() == 0);
  CHECK(harness.captured_samples().empty());

  // The strobe was not lost: it lands on the next slice that has cycles.
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
}

TEST_CASE(
    "Speaker Peripheral: Single Impulse Step Response & Golden Filter Decay") {
  // TC-09: Single Impulse Step Response & Golden Filter Decay
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // One sample per cycle, so a slice one time constant long plus one sample
  // puts the tau pin inside it
  constexpr uint32_t slice_cycles = 23001;
  harness.set_cycles(1000);
  harness.strobe();
  harness.advance_cycles(slice_cycles);
  harness.think(instance, slice_cycles);

  const auto& samples = harness.captured_samples();
  REQUIRE(samples.size() == slice_cycles);

  for (size_t i = 0; i + 1 < samples.size(); ++i) {
    CHECK(samples[i] >= samples[i + 1]);
  }

  CHECK(samples[0] == EDGE_POSITIVE);
  CHECK(samples[0] > 1.0f);

  CHECK(samples[500] ==
        doctest::Approx(EDGE * std::pow(FILTER_A, 500)).epsilon(1e-4));
  CHECK(samples[5000] ==
        doctest::Approx(EDGE * std::pow(FILTER_A, 5000)).epsilon(1e-4));

  // The time-constant pin: one tau of cycles later the edge has fallen to
  // 1/e of itself.
  CHECK(samples[23000] == doctest::Approx(EDGE * 0.36787).epsilon(1e-4));

  CHECK(samples.front() > samples.back());
}

TEST_CASE("Speaker Peripheral: Continuous 1 kHz Audio Tone Golden Synthesis") {
  // TC-11: Continuous 1 kHz Audio Tone Golden Synthesis
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // A half period of 510 cycles is a 1 kHz square wave at the NTSC clock
  constexpr uint32_t half_period = 510;
  constexpr int half_periods = 400;

  harness.set_cycles(0);
  for (int i = 0; i < half_periods; ++i) {
    harness.strobe(instance);
    harness.advance_cycles(half_period);
    harness.think(instance, half_period);
  }

  const auto& samples = harness.captured_samples();
  CHECK(harness.audio_push_count() == half_periods);
  REQUIRE(samples.size() == static_cast<size_t>(half_periods) * half_period);

  CHECK(samples[0] == EDGE_POSITIVE);

  // Steady state: the post-edge peak converges on 2 / (1 + a^N), alternating
  // sign. The two-step map's eigenvalue is a^(2N) = 0.9566 per period, so the
  // peak is within a thousandth of the plateau after about 155 periods.
  const double plateau = EDGE / (1.0 + std::pow(FILTER_A, half_period));
  CHECK(plateau == doctest::Approx(1.01109).epsilon(1e-5));
  for (int i = 350; i < half_periods; ++i) {
    const double sign = 1.0 - (2.0 * (i % 2));
    CHECK(samples[static_cast<size_t>(i) * half_period] ==
          doctest::Approx(sign * plateau).epsilon(1e-3));
  }
}

TEST_CASE("Speaker Peripheral: Cone Decay Reaches Silence And Stops") {
  // TC-12: the DC blocker is the only decay there is. A single edge decays to
  // the silence epsilon and the speaker then pushes nothing at all.
  SpeakerHarness_t harness;
  // The clock is set before init so that the reset phase lands on the strobe
  // and the very first captured sample is the edge, with no leading silence.
  harness.set_cycles(1000);
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.strobe();

  // Eleven frames of thinking carry audio and the twelfth has nothing left to
  // say, because the edge reaches the silence epsilon inside the eleventh.
  for (int step = 0; step < FRAMES_TO_SILENCE; ++step) {
    harness.advance_cycles(NTSC_FRAME_CYCLES);
    harness.think(instance, NTSC_FRAME_CYCLES);
  }
  CHECK(harness.audio_push_count() == FRAMES_TO_SILENCE);

  const auto& tail = harness.captured_samples();
  REQUIRE(tail.size() ==
          static_cast<size_t>(FRAMES_TO_SILENCE) * NTSC_FRAME_CYCLES);
  CHECK(tail[0] == EDGE_POSITIVE);

  const size_t first_silent = static_cast<size_t>(
      std::find(tail.begin(), tail.end(), 0.0f) - tail.begin());
  REQUIRE(first_silent < tail.size());

  // The DC blocker is the whole decay: with nothing driving the cone the
  // recurrence collapses to filter_state *= a, sample after sample, with no
  // threshold and no second model anywhere.
  for (size_t k = 0; k + 1 < first_silent; ++k) {
    CHECK(tail[k + 1] == doctest::Approx(FILTER_A * tail[k]).epsilon(1e-6));
  }

  // The snap fires on the first sample below the epsilon, so the last audible
  // one sits above it and one more step would have fallen through.
  const float last_audible = tail[first_silent - 1];
  CHECK(last_audible >= SILENCE_EPSILON);
  CHECK(last_audible * static_cast<float>(FILTER_A) < SILENCE_EPSILON);

  // Cycles from the edge to the cutoff: the first k with EDGE * a^k below the
  // epsilon. The time-constant approximation, tau * ln(EDGE / epsilon), gives
  // 174821; dividing by -ln(a) instead of by 1/tau is four samples shorter,
  // and the exact count is what the recurrence actually produces.
  const size_t predicted_silent = static_cast<size_t>(
      std::ceil(std::log(EDGE / SILENCE_EPSILON) / -std::log(FILTER_A)));
  CHECK(predicted_silent == 174817);
  CHECK(first_silent == predicted_silent);

  // Everything after the snap is true zero, and nothing follows the push.
  for (size_t k = first_silent; k < tail.size(); ++k) {
    CHECK(tail[k] == 0.0f);
  }

  harness.advance_cycles(NTSC_FRAME_CYCLES);
  harness.think(instance, NTSC_FRAME_CYCLES);
  CHECK(harness.audio_push_count() == FRAMES_TO_SILENCE);
}

// =============================================================================
// Domain 5: Host Seam Decoupling & Output Pacing
// =============================================================================

TEST_CASE("Speaker Peripheral: Host Seam Null Callback Fault Tolerance") {
  // TC-13: Host Seam Null Callback Fault Tolerance
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // 1. AudioPushChannels == nullptr drops the push and changes nothing else:
  // the cone keeps decaying underneath, so restoring the callback resumes the
  // same tail rather than a fresh edge.
  harness.set_drop_audio(true);
  harness.set_cycles(1000);
  harness.strobe();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  CHECK(harness.audio_push_count() == 0);
  CHECK(harness.captured_samples().empty());
  harness.set_drop_audio(false);

  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples()[0] ==
        doctest::Approx(EDGE * std::pow(FILTER_A, 1000)).epsilon(1e-4));
  harness.clear_captured_samples();

  // 2. GetCycles == nullptr must fall back to 0 without crashing
  harness.set_null_cycles(true);
  harness.strobe();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  harness.set_null_cycles(false);

  HostInterface_t null_direct_host{};
  void* no_direct_inst =
      speaker_get_descriptor()->init(TEST_SLOT, &null_direct_host);
  REQUIRE(no_direct_inst != nullptr);
  speaker_get_descriptor()->shutdown(no_direct_inst);

  harness.set_null_strobe_registration(true);
  void* legacy_host_inst = harness.create_speaker(TEST_SLOT);
  REQUIRE(legacy_host_inst != nullptr);
  CHECK(harness.legacy_handlers_empty());
  harness.set_null_strobe_registration(false);
}

TEST_CASE(
    "Speaker Peripheral: Seam 3 Deterministic 1-Second Audio Sample Pacing") {
  // TC-14: Seam 3 Deterministic 1-Second Audio Sample Pacing
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // 1. One sample per cycle: a standard Apple II frame is 17,030 samples
  // Pacing is only observable while the cone is driven
  harness.strobe();
  harness.advance_cycles(NTSC_FRAME_CYCLES);
  harness.think(instance, NTSC_FRAME_CYCLES);

  CHECK(harness.captured_samples().size() == NTSC_FRAME_CYCLES);
  CHECK(harness.audio_push_count() == 1);
  CHECK(harness.captured_channels() == 1);

  harness.clear_captured_samples();
  harness.advance_cycles(NTSC_ONE_SECOND_CYCLES);
  harness.think(instance, static_cast<uint32_t>(NTSC_ONE_SECOND_CYCLES));

  CHECK(harness.captured_samples().size() == SPEAKER_MAX_SAMPLES_PER_UPDATE);
  CHECK(harness.audio_push_count() == 1);
}

// =============================================================================
// Domain 6: Snapshot Persistence & Save-State Round-Trip
// =============================================================================

TEST_CASE("Speaker Peripheral: Snapshot Persistence & Sizing Contract") {
  // TC-15: Two-Pass Sizing Contract
  SpeakerHarness_t harness;
  void* instance1 = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance1 != nullptr);

  harness.set_cycles(1000);
  harness.strobe();
  harness.advance_cycles(100);
  harness.think(instance1, 100);
  REQUIRE(harness.audio_push_count() == 1);
  harness.clear_captured_samples();

  size_t state_size = 0;
  PeripheralStatus_t status =
      speaker_get_descriptor()->save_state(instance1, nullptr, &state_size);
  CHECK(status == peripheral_ok);
  CHECK(state_size == sizeof(SsIoSpeaker_t));

  std::vector<uint8_t> buffer(state_size);
  status = speaker_get_descriptor()->save_state(instance1, buffer.data(),
                                                &state_size);
  CHECK(status == peripheral_ok);

  // TC-16: Bit-for-Bit State Preservation & Cross-Instance Restoration
  void* instance2 = harness.create_speaker(TEST_SLOT + 1);
  REQUIRE(instance2 != nullptr);

  harness.think(instance2, 100);
  CHECK(harness.audio_push_count() == 0);

  status = speaker_get_descriptor()->load_state(instance2, buffer.data(),
                                                state_size);
  CHECK(status == peripheral_ok);

  // Verify save-state round-trip data equality
  size_t state_size2 = 0;
  speaker_get_descriptor()->save_state(instance2, nullptr, &state_size2);
  CHECK(state_size2 == sizeof(SsIoSpeaker_t));
  std::vector<uint8_t> buffer2(state_size2);
  speaker_get_descriptor()->save_state(instance2, buffer2.data(), &state_size2);
  CHECK(buffer == buffer2);

  // The differential proof of restore: from here, identical strobe and think
  // sequences on both instances produce identical output. This says what a
  // struct-field comparison cannot, that the restored cone behaves the same.
  harness.strobe(instance1);
  harness.strobe(instance2);
  harness.advance_cycles(500);

  harness.clear_captured_samples();
  harness.think(instance1, 500);
  const std::vector<float> from_original = harness.captured_samples();
  REQUIRE_FALSE(from_original.empty());

  harness.clear_captured_samples();
  harness.think(instance2, 500);

  // SsIoSpeaker_t stores filter_state as float while the live filter runs in
  // double, so the restored cone tracks the original to float precision rather
  // than bit for bit. That ceiling is the .aws format's, not the restore's.
  const auto& from_restored = harness.captured_samples();
  REQUIRE(from_restored.size() == from_original.size());
  for (size_t i = 0; i < from_original.size(); ++i) {
    CHECK(from_restored[i] == doctest::Approx(from_original[i]).epsilon(1e-6));
  }
}

TEST_CASE("Speaker Peripheral: Anti-DC Pop Observable Output Verification") {
  // TC-17: Anti-DC Pop Observable Output Verification
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // A snapshot taken with the flip-flop high and the cone already settled
  SsIoSpeaker_t pop_state{};
  pop_state.state = 1;
  pop_state.last_sample_state = 1;
  pop_state.filter_state = 0.0f;
  const PeripheralStatus_t status = speaker_get_descriptor()->load_state(
      instance, &pop_state, sizeof(pop_state));
  CHECK(status == peripheral_ok);

  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  CHECK(harness.audio_push_count() == 0);

  // The first edge after the restore is a full step. A previous_input left at
  // zero instead of the restored drive level would give half of one.
  harness.strobe();
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples()[0] == EDGE_NEGATIVE);
}

TEST_CASE("Speaker Peripheral: Pre-Restore Event Queue Purge") {
  // TC-18: Pre-Restore Event Queue Purge
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // Strobe 500 times
  for (int i = 0; i < 500; ++i) {
    harness.strobe(instance);
    harness.advance_cycles(2);
  }

  // Restore quiet snapshot
  SsIoSpeaker_t quiet_state{};
  const PeripheralStatus_t status = speaker_get_descriptor()->load_state(
      instance, &quiet_state, sizeof(quiet_state));
  CHECK(status == peripheral_ok);

  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  CHECK(harness.audio_push_count() == 0);
  CHECK(harness.captured_samples().empty());
}

// =============================================================================
// Domain 7: Numerical Hardening, Stress Boundaries & Error Recovery
// =============================================================================

TEST_CASE("Speaker Peripheral: Queue Overflow Keeps The Final Polarity") {
  // TC-19: the flip-flop always toggles, so an overflowing queue overwrites
  // its own tail. An even number of strobes in one instant leaves the cone
  // where it was, which is only true if the overflowing strobes still flipped
  // the latch. A queue that dropped them would drive high and step by 2.0.
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // Capacity plus one pair, so the parity is even.
  constexpr size_t overflowing_strobes = SPEAKER_MAX_EVENTS_PER_UPDATE + 2;
  harness.set_cycles(1000);
  for (size_t i = 0; i < overflowing_strobes; ++i) {
    harness.strobe(instance);
  }
  harness.advance_cycles(1000);
  harness.think(instance, 1000);

  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples().size() == 1000);
  for (float s : harness.captured_samples()) {
    CHECK(s == 0.0f);
  }
}

TEST_CASE("Speaker Peripheral: Queue Overflow Keeps An Odd Parity Too") {
  // The companion to the even case: capacity plus one strobe is an odd
  // count, so the latch ends high and the cone takes a full edge. Recording
  // exactly capacity and dropping the rest would leave it low and silent.
  SpeakerHarness_t harness;
  harness.set_cycles(1000);
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  constexpr size_t overflowing_strobes = SPEAKER_MAX_EVENTS_PER_UPDATE + 1;
  for (size_t i = 0; i < overflowing_strobes; ++i) {
    harness.strobe(instance);
  }
  harness.advance_cycles(1000);
  harness.think(instance, 1000);

  REQUIRE(harness.audio_push_count() == 1);
  REQUIRE(harness.captured_samples().size() == 1000);
  CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
  for (float s : harness.captured_samples()) {
    CHECK(std::isfinite(s));
  }
}

TEST_CASE("Speaker Peripheral: Backwards Clock & Cycle Underflow Clamping") {
  // TC-20: Backwards Clock & Cycle Underflow Clamping
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(10);
  harness.think(instance, 1000);  // 10 < 1000: start_cycle clamped to 0
}

TEST_CASE("Speaker Peripheral: Poisoned Save-State Recovery") {
  // TC-21: Poisoned Save-State Recovery (NaN / Inf / Negative Clock)
  auto* descriptor = speaker_get_descriptor();
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // Every poisoned field the .aws format can carry, one at a time. After each
  // one the scrubbed instance must behave like a cone at rest: silent until
  // strobed, then a full edge with nothing but finite values behind it.
  const std::vector<SsIoSpeaker_t> poisoned = [] {
    std::vector<SsIoSpeaker_t> states(6);
    states[0].filter_state = NAN;
    states[1].filter_state = INFINITY;
    states[2].filter_state = -INFINITY;
    states[3].next_sample_cycle = NAN;
    states[4].next_sample_cycle = -100.0;
    states[5].next_sample_cycle = 1e300;
    return states;
  }();

  for (const SsIoSpeaker_t& state : poisoned) {
    CHECK(descriptor->load_state(instance, &state, sizeof(SsIoSpeaker_t)) ==
          peripheral_ok);

    harness.clear_captured_samples();
    harness.strobe();
    harness.advance_cycles(100);
    harness.think(instance, 100);

    REQUIRE(harness.audio_push_count() == 1);
    REQUIRE(harness.captured_samples().size() == 100);
    CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
    for (float s : harness.captured_samples()) {
      CHECK(std::isfinite(s));
    }
  }
}

TEST_CASE("Speaker Peripheral: A Refused Load Leaves The Instance Untouched") {
  // load_state is all-or-nothing: a wrong size is rejected before any field
  // is written, so the refused instance stays bit-identical in behavior to
  // one that never saw the call.
  auto* descriptor = speaker_get_descriptor();
  SpeakerHarness_t harness;
  harness.set_cycles(1000);
  void* refused = harness.create_speaker(TEST_SLOT);
  void* untouched = harness.create_speaker(TEST_SLOT + 1);
  REQUIRE(refused != nullptr);
  REQUIRE(untouched != nullptr);

  SsIoSpeaker_t driven{};
  driven.state = 1;
  driven.last_sample_state = 1;
  driven.filter_state = 1.5f;
  driven.next_sample_cycle = 1000.0;

  CHECK(descriptor->load_state(refused, &driven, sizeof(SsIoSpeaker_t) - 1) ==
        peripheral_error);
  CHECK(descriptor->load_state(refused, &driven, sizeof(SsIoSpeaker_t) + 1) ==
        peripheral_error);
  CHECK(descriptor->load_state(refused, &driven, 0) == peripheral_error);

  harness.strobe(refused);
  harness.strobe(untouched);
  harness.advance_cycles(500);

  harness.clear_captured_samples();
  harness.think(refused, 500);
  const std::vector<float> from_refused = harness.captured_samples();

  harness.clear_captured_samples();
  harness.think(untouched, 500);
  const std::vector<float>& from_untouched = harness.captured_samples();

  REQUIRE(from_refused.size() == from_untouched.size());
  REQUIRE_FALSE(from_refused.empty());
  for (size_t i = 0; i < from_refused.size(); ++i) {
    CHECK(from_refused[i] == from_untouched[i]);
  }
}

TEST_CASE("Speaker Peripheral: Save-State Byte Format Pin") {
  // The .aws format is a raw struct dump, so its byte layout is the promise.
  // The two fields the inactivity watchdog used to back are written as
  // constants and ignored on load; this fixture pins both the constants and
  // the offsets. Little-endian, as every platform this project builds on is.
  auto* descriptor = speaker_get_descriptor();
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  REQUIRE(sizeof(SsIoSpeaker_t) == 40);

  SsIoSpeaker_t restored{};
  restored.g_spkr_last_cycle = 0x1234;
  restored.quiet_cycle_count = 0xDEAD;
  restored.recently_active = 7;
  restored.state = 1;
  restored.next_sample_cycle = 4660.0;
  restored.last_sample_state = 1;
  restored.filter_state = 0.5f;
  REQUIRE(descriptor->load_state(instance, &restored, sizeof(restored)) ==
          peripheral_ok);

  std::array<uint8_t, 40> written{};
  size_t written_size = written.size();
  REQUIRE(descriptor->save_state(instance, written.data(), &written_size) ==
          peripheral_ok);
  CHECK(written_size == sizeof(SsIoSpeaker_t));

  const std::array<uint8_t, 40> golden = {
      // g_spkr_last_cycle = 0x1234
      0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // quiet_cycle_count, a dead field written as zero
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // recently_active, a dead field written as zero
      0x00, 0x00, 0x00, 0x00,
      // state = 1
      0x01, 0x00, 0x00, 0x00,
      // next_sample_cycle = 4660.0
      0x00, 0x00, 0x00, 0x00, 0x00, 0x34, 0xB2, 0x40,
      // last_sample_state = 1
      0x01, 0x00, 0x00, 0x00,
      // filter_state = 0.5f
      0x00, 0x00, 0x00, 0x3F};
  CHECK(std::memcmp(written.data(), golden.data(), golden.size()) == 0);
}

TEST_CASE(
    "Speaker Peripheral: Robustness Across Null Pointers and Undersized "
    "Buffers") {
  // TC-22: Robustness Across Null Pointers and Undersized Buffers
  auto* descriptor = speaker_get_descriptor();
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  CHECK(descriptor->init(TEST_SLOT, nullptr) == nullptr);

  size_t size = 0;
  CHECK(descriptor->query(instance, PERIPHERAL_QUERY_AUDIO_INFO, nullptr,
                          &size) == peripheral_ok);
  CHECK(size == sizeof(PeripheralAudioInfo_t));

  PeripheralAudioInfo_t info{};
  size_t small_size = 0;
  CHECK(descriptor->query(instance, PERIPHERAL_QUERY_AUDIO_INFO, &info,
                          &small_size) == peripheral_error);
  CHECK(descriptor->query(instance, PERIPHERAL_QUERY_AUDIO_INFO, &info,
                          nullptr) == peripheral_error);

  size = sizeof(PeripheralAudioInfo_t);
  CHECK(descriptor->query(instance, 0xFFFF, &info, &size) ==
        peripheral_incompatible);

  uint8_t tiny_buf[1] = {0};
  size = sizeof(tiny_buf);
  CHECK(descriptor->save_state(instance, tiny_buf, &size) == peripheral_error);
  CHECK(descriptor->save_state(instance, tiny_buf, nullptr) ==
        peripheral_error);
  CHECK(descriptor->load_state(instance, tiny_buf, sizeof(tiny_buf)) ==
        peripheral_error);
  CHECK(descriptor->load_state(instance, nullptr, sizeof(SsIoSpeaker_t)) ==
        peripheral_error);

  std::vector<uint8_t> oversized_buf(sizeof(SsIoSpeaker_t) + 16, 0);
  CHECK(descriptor->load_state(instance, oversized_buf.data(),
                               oversized_buf.size()) == peripheral_error);

  // Null instance handling across all API endpoints. The audio layout is a
  // property of the peripheral rather than of an instance, so that one query
  // answers without one.
  size = sizeof(PeripheralAudioInfo_t);
  CHECK(descriptor->query(nullptr, PERIPHERAL_QUERY_AUDIO_INFO, &info, &size) ==
        peripheral_ok);
  CHECK(descriptor->save_state(nullptr, tiny_buf, &size) == peripheral_error);
  CHECK(descriptor->load_state(nullptr, tiny_buf, sizeof(tiny_buf)) ==
        peripheral_error);
  descriptor->shutdown(nullptr);
  descriptor->reset(nullptr);
  descriptor->think(nullptr, 100);

  // A strobe delivered to a null instance must be absorbed
  harness.strobe_instance(nullptr);
}

TEST_CASE("Speaker Peripheral: Multiple Strobes in Identical Cycle") {
  // TC-23: Multiple Strobes in Identical Cycle (t1 == t2)
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(5000);
  harness.strobe();
  harness.strobe();
  harness.advance_cycles(50);
  harness.think(instance, 50);

  // Two strobes in one instant are no net edge, so the blocker sees no step.
  // Output still flows: the slice carried events.
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples().size() == 50);
  CHECK(harness.captured_samples()[0] == 0.0f);
}

TEST_CASE("Speaker Peripheral: Immediate Silence on Active Reset") {
  // TC-24: Immediate Silence on Active Reset
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  harness.strobe();
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE(harness.audio_push_count() == 1);
  harness.clear_captured_samples();

  // Reset is idempotent, and it returns the cone to rest mid-decay
  speaker_get_descriptor()->reset(instance);
  speaker_get_descriptor()->reset(instance);

  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  CHECK(harness.audio_push_count() == 0);
  CHECK(harness.captured_samples().empty());
}

TEST_CASE("Speaker Peripheral: Audio Information Query ABI Contract") {
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // 1. Query with null buffer returns required size
  size_t size = 0;
  PeripheralStatus_t status = speaker_get_descriptor()->query(
      instance, PERIPHERAL_QUERY_AUDIO_INFO, nullptr, &size);
  CHECK(status == peripheral_ok);
  CHECK(size == sizeof(PeripheralAudioInfo_t));

  PeripheralAudioInfo_t info{};
  size = 1;
  status = speaker_get_descriptor()->query(
      instance, PERIPHERAL_QUERY_AUDIO_INFO, &info, &size);
  CHECK(status == peripheral_error);
  CHECK(size == sizeof(PeripheralAudioInfo_t));

  size = sizeof(PeripheralAudioInfo_t) - 1;
  status = speaker_get_descriptor()->query(
      instance, PERIPHERAL_QUERY_AUDIO_INFO, &info, &size);
  CHECK(status == peripheral_error);
  CHECK(size == sizeof(PeripheralAudioInfo_t));

  // 3. Query with valid buffer populates self-describing mono speaker info
  size = sizeof(PeripheralAudioInfo_t);
  status = speaker_get_descriptor()->query(
      instance, PERIPHERAL_QUERY_AUDIO_INFO, &info, &size);
  CHECK(status == peripheral_ok);
  // The speaker is CPU-clocked at one sample per cycle and knows no rate in Hz
  CHECK(info.time_base == peripheral_audio_cpu_clocked);
  CHECK(info.cycle_divisor == 1);
  CHECK(info.peak_magnitude == doctest::Approx(2.0f));
  CHECK(info.num_channels == 1);
  CHECK(std::strcmp(info.channels[0].name, "Speaker") == 0);
  CHECK(info.channels[0].default_pan_left == doctest::Approx(1.0f));
  CHECK(info.channels[0].default_pan_right == doctest::Approx(1.0f));
}

// =============================================================================
// Domain 8: Adversarial boundaries
// =============================================================================

TEST_CASE("Speaker Peripheral: Every Slice Length At The Boundaries") {
  // A slice yields exactly as many samples as the smaller of its elapsed
  // cycles and the per-update capacity, at every boundary of that min.
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  const std::vector<uint32_t> slices = {0,
                                        1,
                                        SPEAKER_MAX_SAMPLES_PER_UPDATE - 1,
                                        SPEAKER_MAX_SAMPLES_PER_UPDATE,
                                        SPEAKER_MAX_SAMPLES_PER_UPDATE + 1,
                                        UINT32_MAX};

  for (uint32_t elapsed : slices) {
    // A cone at rest is silent, so the slice has to be driven for its length
    // to be observable at all.
    harness.set_cycles(1ULL << 33);
    speaker_get_descriptor()->reset(instance);
    harness.clear_captured_samples();
    harness.strobe(instance);

    harness.advance_cycles(elapsed);
    harness.think(instance, elapsed);

    const size_t expected = std::min<uint64_t>(
        elapsed, static_cast<uint64_t>(SPEAKER_MAX_SAMPLES_PER_UPDATE));
    CHECK(harness.captured_samples().size() == expected);
    CHECK(harness.audio_push_count() <= 1);
    for (float s : harness.captured_samples()) {
      CHECK(std::isfinite(s));
    }
  }
}

TEST_CASE("Speaker Peripheral: A Hostile Clock Resynchronizes") {
  // The peripheral cannot audit its host. A clock that runs backwards, jumps
  // by half the 64-bit range, or wraps to zero must leave the cone in a state
  // the next ordinary slice can drive again.
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  const std::vector<uint64_t> hostile = {1000, 999, 1000 + (1ULL << 63),
                                         UINT64_MAX, 0};

  for (uint64_t cycle : hostile) {
    harness.set_cycles(cycle);
    harness.strobe(instance);
    harness.think(instance, 100);
    for (float s : harness.captured_samples()) {
      CHECK(std::isfinite(s));
    }
    harness.clear_captured_samples();
  }

  // One ordinary slice later the cone is drivable again, and the next edge
  // from rest is a full step.
  harness.set_cycles(1000000);
  speaker_get_descriptor()->reset(instance);
  harness.clear_captured_samples();
  harness.strobe(instance);
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
}

TEST_CASE("Speaker Peripheral: Every Save-State Field Poisoned In Turn") {
  // The .aws file is attacker-controlled input. Each field gets every value
  // its type can carry that the synthesizer would choke on, one at a time,
  // and after each load the next edge is still a clean full step.
  auto* descriptor = speaker_get_descriptor();
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  const std::vector<double> poison = {
      NAN, INFINITY, -INFINITY, -1.0, 1e300, -1e300, 1e-42, 0.0, 4.9e-324};

  for (double value : poison) {
    const auto as_u64 =
        static_cast<uint64_t>(std::isfinite(value) ? std::fabs(value) : 0.0);
    std::vector<SsIoSpeaker_t> states(7);
    states[0].g_spkr_last_cycle = as_u64;
    states[1].quiet_cycle_count = as_u64;
    states[2].recently_active = static_cast<uint32_t>(as_u64);
    states[3].state = static_cast<uint32_t>(as_u64);
    states[4].next_sample_cycle = value;
    states[5].last_sample_state = static_cast<uint32_t>(as_u64);
    states[6].filter_state = static_cast<float>(value);

    for (const SsIoSpeaker_t& state : states) {
      CHECK(descriptor->load_state(instance, &state, sizeof(SsIoSpeaker_t)) ==
            peripheral_ok);

      harness.clear_captured_samples();
      harness.set_cycles(1ULL << 20);
      harness.strobe(instance);
      harness.advance_cycles(100);
      harness.think(instance, 100);
      for (float s : harness.captured_samples()) {
        CHECK(std::isfinite(s));
      }
    }
  }

  // Sizes off by one in either direction, and zero, are all refused.
  SsIoSpeaker_t any{};
  CHECK(descriptor->load_state(instance, &any, 0) == peripheral_error);
  CHECK(descriptor->load_state(instance, &any, sizeof(any) - 1) ==
        peripheral_error);
  CHECK(descriptor->load_state(instance, &any, sizeof(any) + 1) ==
        peripheral_error);
}

TEST_CASE("Speaker Peripheral: Query Sizes At The Extremes") {
  auto* descriptor = speaker_get_descriptor();
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  PeripheralAudioInfo_t info{};
  size_t size = 0;
  CHECK(descriptor->query(instance, PERIPHERAL_QUERY_AUDIO_INFO, &info,
                          &size) == peripheral_error);
  CHECK(size == sizeof(PeripheralAudioInfo_t));

  // An absurdly large claim is honoured: the peripheral writes what it
  // promised and reports what it wrote, never what was offered.
  size = SIZE_MAX;
  CHECK(descriptor->query(instance, PERIPHERAL_QUERY_AUDIO_INFO, &info,
                          &size) == peripheral_ok);
  CHECK(size == sizeof(PeripheralAudioInfo_t));

  CHECK(descriptor->query(instance, PERIPHERAL_QUERY_AUDIO_INFO, &info,
                          nullptr) == peripheral_error);
}

TEST_CASE("Speaker Peripheral: A Host That Offers Nothing At All") {
  // Every callback null at once. The seam is mandatory only in that a null
  // host is refused outright; a host that answers nothing still yields a
  // working instance that simply cannot be heard.
  auto* descriptor = speaker_get_descriptor();
  HostInterface_t empty_host{};

  void* instance = descriptor->init(TEST_SLOT, &empty_host);
  REQUIRE(instance != nullptr);

  descriptor->reset(instance);
  descriptor->think(instance, 1000);
  descriptor->think(instance, UINT32_MAX);

  size_t size = sizeof(SsIoSpeaker_t);
  std::vector<uint8_t> buffer(size);
  CHECK(descriptor->save_state(instance, buffer.data(), &size) ==
        peripheral_ok);
  CHECK(descriptor->load_state(instance, buffer.data(), size) == peripheral_ok);

  descriptor->shutdown(instance);
}
