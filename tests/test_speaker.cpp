// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "doctest.h"

namespace {

constexpr uint16_t ADDR_SPEAKER = 0xC030;
constexpr int TEST_SLOT = 0;
constexpr uint64_t NTSC_ONE_SECOND_CYCLES = 1022727;

// Every edge is a step of exactly 2.0 through the DC blocker now that
// previous_input tracks the drive level. Transitional: the peripheral still
// scales by 16384 and clips, so both edges arrive at an int16 rail. Task B4
// deletes the scale and the clip and these become +/-2.0.
constexpr int16_t EDGE_POSITIVE = 32767;
constexpr int16_t EDGE_NEGATIVE = -32768;

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
    host_.GetClockHz = mock_get_clock_hz;
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

  auto set_clock_hz(double hz) -> void { clock_hz_ = hz; }

  auto set_null_clock_hz(bool is_null) -> void {
    host_.GetClockHz = is_null ? nullptr : mock_get_clock_hz;
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

  auto captured_samples() const -> const std::vector<int16_t>& {
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
  double clock_hz_ = CLOCK_6502_NTSC;
  std::vector<void*> instances_;
  void* primary_instance_ = nullptr;
  std::map<uint16_t, MockDirectIOHandler_t> handlers_;
  std::map<uint16_t, MockStrobeHandler_t> strobes_;
  std::vector<int16_t> captured_samples_;
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
        // Recover the integer samples the speaker still synthesizes. Dividing
        // and re-multiplying by 32768 is exact, so the goldens are unchanged.
        // Task E1 replaces this capture with float.
        for (size_t i = 0; i < num_samples; ++i) {
          s_active_harness->captured_samples_.push_back(static_cast<int16_t>(
              std::lroundf(channel_buffers[0][i] * 32768.0f)));
        }
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

  static auto mock_get_clock_hz() -> double {
    if (s_active_harness != nullptr) {
      return s_active_harness->clock_hz_;
    }
    return CLOCK_6502;
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

  // Strobing instance1 drives instance1 alone
  harness.set_cycles(1000);
  harness.strobe(instance1);
  harness.advance_cycles(100);
  harness.think(instance1, 100);
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
  harness.clear_captured_samples();

  // instance2 is still at rest, and a cone at rest pushes nothing
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

  harness.strobe();
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
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
  // TC-05: Alternating Flip-Flop Polarity Verification
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // Strobe 1: flips the latch from false to true (+1.0 drive level)
  harness.set_cycles(100);
  harness.strobe(instance);
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples()[0] == EDGE_POSITIVE);
  harness.clear_captured_samples();

  // Strobe 2: flips the latch from true to false (-1.0 drive level, negative
  // transition)
  harness.advance_cycles(50);
  harness.strobe(instance);
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples()[0] < 0);
  harness.clear_captured_samples();

  // Strobe 3: flips the latch from false to true (+1.0 drive level, positive
  // transition)
  harness.advance_cycles(50);
  harness.strobe(instance);
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples()[0] > 0);
  harness.clear_captured_samples();

  // Strobe 4: flips the latch from true to false (-1.0 drive level, negative
  // transition)
  harness.advance_cycles(50);
  harness.strobe(instance);
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples()[0] < 0);
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
}

TEST_CASE(
    "Speaker Peripheral: Single Impulse Step Response & Golden Filter Decay") {
  // TC-09: Single Impulse Step Response & Golden Filter Decay
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  harness.strobe();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);

  const auto& samples = harness.captured_samples();
  REQUIRE_FALSE(samples.empty());

  // Mono planar channel: single discrete stream, strictly monotonic decay
  for (size_t i = 0; i + 1 < samples.size(); ++i) {
    CHECK(samples[i] >= samples[i + 1]);
  }

  // The onset is a step of 2.0, which the transitional scale rails
  CHECK(samples[0] == EDGE_POSITIVE);

  // and the blocker decays it by dc_blocker_coefficient per sample
  constexpr double edge = 2.0;
  constexpr double a = 0.999;
  constexpr double scale = 16384.0;
  CHECK(samples[1] == doctest::Approx(edge * a * scale).epsilon(1e-4));
  CHECK(samples[2] == doctest::Approx(edge * a * a * scale).epsilon(1e-4));

  // Monotonic step decay point checks
  CHECK(samples[0] > samples[1]);
  CHECK(samples[1] > samples[2]);
  CHECK(samples.front() > samples.back());
}

TEST_CASE("Speaker Peripheral: Continuous 1 kHz Audio Tone Golden Synthesis") {
  // TC-11: Continuous 1 kHz Audio Tone Golden Synthesis
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // Toggle every 511 cycles across 22,000 cycles (~1 kHz square wave)
  uint64_t current_cycle = 0;
  for (int period = 0; period < 43; ++period) {
    current_cycle += 511;
    harness.set_cycles(current_cycle);
    harness.strobe(instance);
  }
  harness.advance_cycles(511);
  harness.think(instance, static_cast<uint32_t>(harness.cycles()));

  const auto& samples = harness.captured_samples();
  REQUIRE(samples.size() >= 100);

  // Check active bipolar waveform generation
  const auto min_max = std::minmax_element(samples.begin(), samples.end());
  CHECK(*min_max.first < 0);
  CHECK(*min_max.second > 0);
}

TEST_CASE("Speaker Peripheral: Cone Decay Reaches Silence And Stops") {
  // TC-12: the DC blocker is the only decay there is. A single edge decays to
  // the silence epsilon and the speaker then pushes nothing at all.
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  constexpr uint32_t frame_cycles = 17030;
  harness.set_cycles(1000);
  harness.strobe();

  // tau * ln(2.0 / 0.001) is 174,821 cycles from a 2.0 edge down to the
  // silence epsilon: eleven NTSC frames and part of a twelfth. So eleven
  // thinks carry audio and the twelfth has nothing left to say.
  constexpr int driven_frames = 11;
  for (int step = 0; step < driven_frames; ++step) {
    harness.advance_cycles(frame_cycles);
    harness.think(instance, frame_cycles);
  }
  CHECK(harness.audio_push_count() == driven_frames);

  // The tail is snapped to true zero, and nothing follows it.
  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples().back() == 0);

  harness.advance_cycles(frame_cycles);
  harness.think(instance, frame_cycles);
  CHECK(harness.audio_push_count() == driven_frames);
}

// =============================================================================
// Domain 5: Host Seam Decoupling & Output Pacing
// =============================================================================

TEST_CASE("Speaker Peripheral: Host Seam Null Callback Fault Tolerance") {
  // TC-13: Host Seam Null Callback Fault Tolerance
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // 1. AudioPushChannels == nullptr must safely drop without crashing
  harness.set_drop_audio(true);
  harness.strobe();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  CHECK(harness.captured_samples().empty());
  harness.set_drop_audio(false);

  // 2. GetCycles == nullptr must fall back to 0 without crashing
  harness.set_null_cycles(true);
  harness.strobe();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  harness.set_null_cycles(false);

  // 3. RegisterDirectIOStrobe == nullptr must succeed without crash
  HostInterface_t null_direct_host{};
  void* no_direct_inst =
      speaker_get_descriptor()->init(TEST_SLOT, &null_direct_host);
  REQUIRE(no_direct_inst != nullptr);
  speaker_get_descriptor()->shutdown(no_direct_inst);

  // 4. A host offering only the legacy registration leaves it untouched
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

  // 1. Single frame pacing: standard 17,030 cycle Apple II frame generates
  // 736 samples
  constexpr uint32_t frame_cycles = 17030;
  constexpr size_t expected_frame_samples = 736;
  // Pacing is only observable while the cone is driven
  harness.strobe();
  harness.advance_cycles(frame_cycles);
  harness.think(instance, frame_cycles);

  CHECK(harness.captured_samples().size() == expected_frame_samples);
  CHECK(harness.audio_push_count() == 1);

  // 2. Buffer ceiling guard: a massive single-call delta (1 second) safely
  // clamps to the internal scratch buffer capacity (4096 samples)
  harness.clear_captured_samples();
  constexpr size_t max_single_push_samples = 4096;
  harness.advance_cycles(NTSC_ONE_SECOND_CYCLES);
  harness.think(instance, static_cast<uint32_t>(NTSC_ONE_SECOND_CYCLES));

  CHECK(harness.captured_samples().size() == max_single_push_samples);
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

  // A fresh instance is at rest and pushes nothing before the restore
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
  const std::vector<int16_t> from_original = harness.captured_samples();
  REQUIRE_FALSE(from_original.empty());

  harness.clear_captured_samples();
  harness.think(instance2, 500);
  CHECK(harness.captured_samples() == from_original);
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
  // Restoring a settled cone makes no sound at all
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
  // The purged queue leaves a cone at rest, and a cone at rest is silent
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

  // speaker_max_events_per_update is 8192; this is that plus one pair
  constexpr size_t overflowing_strobes = 8194;
  harness.set_cycles(1000);
  for (size_t i = 0; i < overflowing_strobes; ++i) {
    harness.strobe(instance);
  }
  harness.advance_cycles(1000);
  harness.think(instance, 1000);

  REQUIRE(harness.audio_push_count() == 1);
  REQUIRE_FALSE(harness.captured_samples().empty());
  for (int16_t s : harness.captured_samples()) {
    CHECK(s == 0);
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

  SsIoSpeaker_t corrupted_state{};
  corrupted_state.filter_state = NAN;
  corrupted_state.next_sample_cycle = -100.0;
  CHECK(descriptor->load_state(instance, &corrupted_state,
                               sizeof(SsIoSpeaker_t)) == peripheral_ok);

  corrupted_state.filter_state = INFINITY;
  CHECK(descriptor->load_state(instance, &corrupted_state,
                               sizeof(SsIoSpeaker_t)) == peripheral_ok);

  // Subsequent synthesis must not crash or propagate NaN
  harness.advance_cycles(1000);
  harness.think(instance, 1000);
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
  harness.strobe();  // Consecutive strobe at exact same cycle
  harness.advance_cycles(50);
  harness.think(instance, 50);

  // Two strobes in one instant are no net edge, so the blocker sees no step.
  // Output still flows: the slice carried events.
  REQUIRE(harness.audio_push_count() == 1);
  CHECK(harness.captured_samples()[0] == 0);
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

  // 2. Query with undersized buffer returns error
  PeripheralAudioInfo_t info{};
  size = sizeof(PeripheralAudioInfo_t) - 1;
  status = speaker_get_descriptor()->query(
      instance, PERIPHERAL_QUERY_AUDIO_INFO, &info, &size);
  CHECK(status == peripheral_error);

  // 3. Query with valid buffer populates self-describing mono speaker info
  size = sizeof(PeripheralAudioInfo_t);
  status = speaker_get_descriptor()->query(
      instance, PERIPHERAL_QUERY_AUDIO_INFO, &info, &size);
  CHECK(status == peripheral_ok);
  CHECK(info.sample_rate == 44100);
  CHECK(info.num_channels == 1);
  CHECK(std::strcmp(info.channels[0].name, "Speaker") == 0);
  CHECK(info.channels[0].default_pan_left == doctest::Approx(1.0f));
  CHECK(info.channels[0].default_pan_right == doctest::Approx(1.0f));
}

TEST_CASE(
    "Speaker Peripheral: Dynamic NTSC vs PAL Clock Adaptation via Host "
    "Interface") {
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  SUBCASE("NTSC Clock Rate (1,020,484 Hz)") {
    harness.set_clock_hz(CLOCK_6502_NTSC);
    harness.set_cycles(0);
    harness.strobe();
    harness.advance_cycles(17030);
    harness.think(instance, 17030);
    CHECK(harness.captured_channels() == 1);
    CHECK(harness.captured_samples().size() == 736);
  }

  SUBCASE("PAL Clock Rate (1,015,625 Hz)") {
    harness.set_clock_hz(CLOCK_6502_PAL);
    harness.set_cycles(0);
    harness.strobe();
    harness.advance_cycles(20280);
    harness.think(instance, 20280);
    CHECK(harness.captured_channels() == 1);
    CHECK(harness.captured_samples().size() == 881);
  }

  SUBCASE("Defensive Fallback on Null GetClockHz") {
    harness.set_null_clock_hz(true);
    harness.set_cycles(0);
    harness.strobe();
    harness.advance_cycles(17030);
    harness.think(instance, 17030);
    CHECK(harness.captured_channels() == 1);
    CHECK(harness.captured_samples().size() == 736);
  }

  SUBCASE("Dynamic Clock Switch On The Fly") {
    harness.set_clock_hz(CLOCK_6502_PAL);
    harness.set_cycles(0);
    harness.strobe();
    harness.advance_cycles(20280);
    harness.think(instance, 20280);
    CHECK(harness.captured_channels() == 1);
    CHECK(harness.captured_samples().size() == 881);

    harness.clear_captured_samples();
    harness.set_clock_hz(CLOCK_6502_NTSC);
    harness.strobe();
    harness.advance_cycles(17030);
    harness.think(instance, 17030);
    CHECK(harness.captured_channels() == 1);
    CHECK(harness.captured_samples().size() == 736);
  }
}
