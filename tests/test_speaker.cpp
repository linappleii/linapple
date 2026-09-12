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

#include "apple2/Apple2Types.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "apple2/peripherals/speaker/SpeakerCommands.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

extern "C" auto video_get_scanner_address(uint32_t*, uint32_t) -> uint16_t {
  return 0;
}

#include "apple2/CPU.h"
#include "core/LinAppleCore.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;
auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                     uint32_t cycles) -> uint8_t;
extern void (*g_frontendAudioCB)(const int16_t* buffer, size_t count);

namespace {

constexpr uint16_t ADDR_SPEAKER = 0xC030;
constexpr int TEST_SLOT = 0;
constexpr int16_t SPEAKER_PEAK_AMPLITUDE = 0x4000;
constexpr uint64_t INACTIVITY_THRESHOLD_CYCLES =
    static_cast<uint64_t>(CLOCK_6502 / 5.0);
constexpr uint64_t NTSC_ONE_SECOND_CYCLES = 1022727;
constexpr size_t NTSC_ONE_SECOND_STEREO_SAMPLES = 88200;

struct ScopedCpuContext_t {
  CpuInstance_t* previous = nullptr;
  CpuInstance_t fresh{};

  ScopedCpuContext_t() : previous(cpu_get_active_context()) {
    cpu_set_active_context(&fresh);
  }

  ~ScopedCpuContext_t() {
    if (previous != nullptr) {
      cpu_set_active_context(previous);
    }
  }

  ScopedCpuContext_t(const ScopedCpuContext_t&) = delete;
  auto operator=(const ScopedCpuContext_t&) -> ScopedCpuContext_t& = delete;
  ScopedCpuContext_t(ScopedCpuContext_t&&) = delete;
  auto operator=(ScopedCpuContext_t&&) -> ScopedCpuContext_t& = delete;
};

struct MockDirectIOHandler_t {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockDirectIOHandler_t() = default;
  MockDirectIOHandler_t(void* inst, PeripheralIOHandler r,
                        PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
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
    host_.AudioPushSamples = mock_audio_push_samples;
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
    captured_samples_.clear();
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

  auto set_drop_audio(bool drop) -> void {
    host_.AudioPushSamples = drop ? nullptr : mock_audio_push_samples;
  }

  auto set_null_cycles(bool null_cycles) -> void {
    host_.GetCycles = null_cycles ? nullptr : mock_get_cycles;
  }

  auto has_handler(uint16_t addr) const -> bool {
    return handlers_.find(addr) != handlers_.end();
  }

  auto get_handler(uint16_t addr) const -> const MockDirectIOHandler_t& {
    return handlers_.at(addr);
  }

  auto read_io(uint16_t addr, void* inst = nullptr, uint8_t floating_bus = 0)
      -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      void* target = (inst != nullptr)                  ? inst
                     : (it->second.instance != nullptr) ? it->second.instance
                                                        : primary_instance_;
      return it->second.read(target, 0, addr, 0, floating_bus, 0);
    }
    return floating_bus;
  }

  auto write_io(uint16_t addr, void* inst = nullptr, uint8_t val = 0)
      -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      void* target = (inst != nullptr)                  ? inst
                     : (it->second.instance != nullptr) ? it->second.instance
                                                        : primary_instance_;
      return it->second.write(target, 0, addr, 1, val, 0);
    }
    return 0;
  }

  auto toggle_read(void* inst = nullptr, uint8_t floating_bus = 0) -> uint8_t {
    return read_io(ADDR_SPEAKER, inst, floating_bus);
  }

  auto toggle_write(void* inst = nullptr, uint8_t val = 0) -> uint8_t {
    return write_io(ADDR_SPEAKER, inst, val);
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

  auto is_active(void* inst = nullptr) const -> bool {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (target == nullptr) {
      return false;
    }
    uint8_t active = 0;
    size_t size = sizeof(active);
    PeripheralStatus_t status = speaker_get_descriptor()->query(
        target, speaker_query_is_active, &active, &size);
    return (status == peripheral_ok && active != 0);
  }

 private:
  HostInterface_t host_{};
  uint64_t cycles_ = 0;
  std::vector<void*> instances_;
  void* primary_instance_ = nullptr;
  std::map<uint16_t, MockDirectIOHandler_t> handlers_;
  std::vector<int16_t> captured_samples_;
  uint32_t audio_push_count_ = 0;

  static SpeakerHarness_t* s_active_harness;

  static auto mock_log(void* instance, PeripheralLogLevel_t level,
                       const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    (void)fmt;
  }

  static auto mock_assert_irq(int slot, bool assert_irq) -> void {
    (void)slot;
    (void)assert_irq;
  }

  static auto mock_register_direct_io(void* instance, uint16_t addr,
                                      PeripheralIOHandler read,
                                      PeripheralIOHandler write) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->handlers_[addr] = {instance, read, write};
    }
  }

  static auto mock_audio_push_samples(void* instance, const int16_t* buffer,
                                      size_t num_samples) -> void {
    (void)instance;
    if (s_active_harness != nullptr && buffer != nullptr && num_samples > 0) {
      s_active_harness->captured_samples_.insert(
          s_active_harness->captured_samples_.end(), buffer,
          buffer + num_samples);
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

  // Strobing instance1 flips state and asserts activity solely on instance1
  harness.toggle_read(instance1);
  CHECK(harness.is_active(instance1));
  CHECK_FALSE(harness.is_active(instance2));

  // Strobing instance2 flips state and asserts activity on instance2
  harness.toggle_write(instance2, 0x55);
  CHECK(harness.is_active(instance2));

  // Deallocating instance1 leaves instance2 operational
  harness.shutdown_instance(instance1);
  CHECK(harness.is_active(instance2));
  harness.shutdown_instance(instance2);
}

// =============================================================================
// Domain 2: Motherboard Direct I/O & Physical Strobe Mechanics
// =============================================================================

TEST_CASE("Speaker Peripheral: Direct IO Read Strobe & Floating Bus Noise") {
  // TC-03: Direct I/O Read Strobe & Floating Bus Noise
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  CHECK_FALSE(harness.is_active(instance));

  const uint8_t read_res = harness.toggle_read();
  CHECK(read_res == mem_read_floating_bus(0));
  CHECK(harness.is_active(instance));
}

TEST_CASE(
    "Speaker Peripheral: Direct IO Write Strobe & Data Byte Irrelevance") {
  // TC-04: Direct I/O Write Strobe & Data Byte Irrelevance
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  CHECK(harness.toggle_write(instance, 0x00) == mem_read_floating_bus(0));
  harness.advance_cycles(50);
  CHECK(harness.toggle_write(instance, 0x55) == mem_read_floating_bus(0));
  harness.advance_cycles(50);
  CHECK(harness.toggle_write(instance, 0xAA) == mem_read_floating_bus(0));
  harness.advance_cycles(50);
  CHECK(harness.toggle_write(instance, 0xFF) == mem_read_floating_bus(0));
  CHECK(harness.is_active(instance));
}

TEST_CASE("Speaker Peripheral: Alternating Flip-Flop Polarity Verification") {
  // TC-05: Alternating Flip-Flop Polarity Verification
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // Strobe 1: Read flips latch from false to true (+1.0 impulse)
  harness.set_cycles(100);
  harness.toggle_read(instance);
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples()[0] == SPEAKER_PEAK_AMPLITUDE);
  harness.clear_captured_samples();

  // Strobe 2: Read flips latch from true to false (-1.0 impulse, negative
  // transition)
  harness.advance_cycles(50);
  harness.toggle_read(instance);
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples()[0] < 0);
  harness.clear_captured_samples();

  // Strobe 3: Write flips latch from false to true (+1.0 impulse, positive
  // transition)
  harness.advance_cycles(50);
  harness.toggle_write(instance, 0x55);
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples()[0] > 0);
  harness.clear_captured_samples();

  // Strobe 4: Write flips latch from true to false (-1.0 impulse, negative
  // transition)
  harness.advance_cycles(50);
  harness.toggle_write(instance, 0xAA);
  harness.advance_cycles(100);
  harness.think(instance, 100);
  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples()[0] < 0);
}

// =============================================================================
// Domain 3: Clock Synchronization & Inactivity Tracking
// =============================================================================

TEST_CASE(
    "Speaker Peripheral: Inactivity Boundary Threshold (Single-Cycle "
    "Precision)") {
  // TC-06: Inactivity Boundary Threshold (Single-Cycle Precision)
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  CHECK_FALSE(harness.is_active(instance));

  harness.toggle_read();
  harness.think(instance, 0);
  CHECK(harness.is_active(instance));

  // Advance exactly to the inactivity threshold: must remain active
  harness.advance_cycles(INACTIVITY_THRESHOLD_CYCLES);
  harness.think(instance, static_cast<uint32_t>(INACTIVITY_THRESHOLD_CYCLES));
  CHECK(harness.is_active(instance));

  // Advance by 1 more cycle: quiet_cycle_count > threshold triggers
  // deactivation
  harness.advance_cycles(1);
  harness.think(instance, 1);
  CHECK_FALSE(harness.is_active(instance));
}

TEST_CASE("Speaker Peripheral: Inactivity Extension via Mid-Flight Re-Strobe") {
  // TC-07: Inactivity Extension via Mid-Flight Re-Strobe
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(0);
  harness.toggle_read();
  harness.think(instance, 0);
  CHECK(harness.is_active(instance));

  // Advance 150,000 cycles, then re-strobe mid-flight
  constexpr uint32_t mid_flight_cycles = 150000;
  harness.advance_cycles(mid_flight_cycles);
  harness.think(instance, mid_flight_cycles);
  CHECK(harness.is_active(instance));

  harness.toggle_read();
  harness.think(instance, 0);
  CHECK(harness.is_active(instance));

  // Advance by full threshold from the re-strobe point: must remain active
  harness.advance_cycles(INACTIVITY_THRESHOLD_CYCLES);
  harness.think(instance, static_cast<uint32_t>(INACTIVITY_THRESHOLD_CYCLES));
  CHECK(harness.is_active(instance));

  // Advance 1 additional cycle past re-armed threshold: must deactivate
  harness.advance_cycles(1);
  harness.think(instance, 1);
  CHECK_FALSE(harness.is_active(instance));
}

// =============================================================================
// Domain 4: Digital Signal Processing & Filter Mechanics
// =============================================================================

TEST_CASE(
    "Speaker Peripheral: Inactive Digital Silence & Deterministic Sample "
    "Pacing") {
  // TC-08: Inactive Digital Silence & Deterministic Sample Pacing
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  harness.think(instance, 1000);

  // At 44.1 kHz, 1000 cycles produces exactly 88 samples (44 stereo pairs)
  constexpr size_t expected_idle_samples = 88;
  REQUIRE(harness.captured_samples().size() == expected_idle_samples);
  for (int16_t s : harness.captured_samples()) {
    CHECK(s == 0);
  }
}

TEST_CASE(
    "Speaker Peripheral: Single Impulse Step Response & Golden Filter Decay") {
  // TC-09: Single Impulse Step Response & Golden Filter Decay
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  harness.toggle_read();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);

  const auto& samples = harness.captured_samples();
  REQUIRE_FALSE(samples.empty());
  REQUIRE(samples.size() % 2 == 0);

  // Stereo channel equivalence: left and right channels match bit-for-bit
  for (size_t i = 0; i < samples.size(); i += 2) {
    CHECK(samples[i] == samples[i + 1]);
  }

  // Exact step response golden checks
  CHECK(samples[0] == SPEAKER_PEAK_AMPLITUDE);
  CHECK(samples[1] == SPEAKER_PEAK_AMPLITUDE);

  // Exact golden step response decay samples (0.999f filter factor)
  constexpr int16_t golden_decay_sample_1 = 16367;
  constexpr int16_t golden_decay_sample_2 = 16351;
  CHECK(samples[2] == golden_decay_sample_1);
  CHECK(samples[3] == golden_decay_sample_1);
  CHECK(samples[4] == golden_decay_sample_2);
  CHECK(samples[5] == golden_decay_sample_2);

  // Monotonic step decay point checks
  CHECK(samples[0] > samples[2]);
  CHECK(samples[2] > samples[4]);
  CHECK(samples.front() > samples.back());
}

TEST_CASE(
    "Speaker Peripheral: Sub-Cycle Boxcar Area Integration (Fractional Duty "
    "Cycle)") {
  // TC-10: Sub-Cycle Boxcar Area Integration (Fractional Duty Cycle)
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // Strobe 1 at cycle 6 (flips state to true)
  harness.set_cycles(6);
  harness.toggle_read(instance);

  // Strobe 2 at cycle 17 (flips state to false)
  harness.set_cycles(17);
  harness.toggle_read(instance);

  // Advance past first sample window (24 cycles > 23.191 cycles)
  harness.set_cycles(24);
  harness.think(instance, 24);

  const auto& samples = harness.captured_samples();
  REQUIRE(samples.size() >= 2);
  // Mathematical golden value: area average produces exactly -807
  constexpr int16_t golden_boxcar_sample = -807;
  CHECK(samples[0] == golden_boxcar_sample);
  CHECK(samples[1] == golden_boxcar_sample);
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
    harness.toggle_read(instance);
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

TEST_CASE("Speaker Peripheral: Spindown Decay to Absolute Silence") {
  // TC-12: Spindown Decay to Absolute Silence
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  harness.toggle_read();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  harness.clear_captured_samples();

  constexpr uint32_t long_wait_cycles = 2000000;
  harness.advance_cycles(long_wait_cycles);
  harness.think(instance, long_wait_cycles);

  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples().back() == 0);
}

// =============================================================================
// Domain 5: Host Seam Decoupling & Output Pacing
// =============================================================================

TEST_CASE("Speaker Peripheral: Host Seam Null Callback Fault Tolerance") {
  // TC-13: Host Seam Null Callback Fault Tolerance
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // 1. AudioPushSamples == nullptr must safely drop without crashing
  harness.set_drop_audio(true);
  harness.toggle_read();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  CHECK(harness.captured_samples().empty());
  harness.set_drop_audio(false);

  // 2. GetCycles == nullptr must fall back to 0 without crashing
  harness.set_null_cycles(true);
  harness.toggle_read();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  harness.set_null_cycles(false);

  // 3. RegisterDirectIO == nullptr must succeed without crash
  HostInterface_t null_direct_host{};
  void* no_direct_inst =
      speaker_get_descriptor()->init(TEST_SLOT, &null_direct_host);
  REQUIRE(no_direct_inst != nullptr);
  speaker_get_descriptor()->shutdown(no_direct_inst);
}

TEST_CASE(
    "Speaker Peripheral: Seam 3 Deterministic 1-Second Audio Sample Pacing") {
  // TC-14: Seam 3 Deterministic 1-Second Audio Sample Pacing
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // 1. Single frame pacing: standard 17,030 cycle Apple II frame generates
  // 1,472 samples
  constexpr uint32_t frame_cycles = 17030;
  constexpr size_t expected_frame_samples = 1472;
  harness.advance_cycles(frame_cycles);
  harness.think(instance, frame_cycles);

  CHECK(harness.captured_samples().size() == expected_frame_samples);
  CHECK(harness.audio_push_count() == 1);

  // 2. Buffer ceiling guard: a massive single-call delta (1 second) safely
  // clamps to the internal buffer capacity (speaker_buffer_size - 2 = 16382
  // samples)
  harness.clear_captured_samples();
  constexpr size_t max_single_push_samples = 16382;
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
  harness.toggle_read();
  harness.think(instance1, 0);
  REQUIRE(harness.is_active(instance1));

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
  CHECK_FALSE(harness.is_active(instance2));

  status = speaker_get_descriptor()->load_state(instance2, buffer.data(),
                                                state_size);
  CHECK(status == peripheral_ok);
  CHECK(harness.is_active(instance2));

  // Verify save-state round-trip data equality
  size_t state_size2 = 0;
  speaker_get_descriptor()->save_state(instance2, nullptr, &state_size2);
  CHECK(state_size2 == sizeof(SsIoSpeaker_t));
  std::vector<uint8_t> buffer2(state_size2);
  speaker_get_descriptor()->save_state(instance2, buffer2.data(), &state_size2);
  CHECK(buffer == buffer2);
}

TEST_CASE("Speaker Peripheral: Anti-DC Pop Observable Output Verification") {
  // TC-17: Anti-DC Pop Observable Output Verification
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  SsIoSpeaker_t pop_state{};
  pop_state.last_sample_state = 1;
  pop_state.filter_state = 0.0f;
  pop_state.recently_active = 0;
  const PeripheralStatus_t status = speaker_get_descriptor()->load_state(
      instance, &pop_state, sizeof(pop_state));
  CHECK(status == peripheral_ok);

  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  REQUIRE_FALSE(harness.captured_samples().empty());
  // Pop suppression verified: first sample is clean zero, not spurious +16384
  // step
  CHECK(harness.captured_samples()[0] == 0);
}

TEST_CASE("Speaker Peripheral: Pre-Restore Event Queue Purge") {
  // TC-18: Pre-Restore Event Queue Purge
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // Strobe 500 times
  for (int i = 0; i < 500; ++i) {
    harness.toggle_read(instance);
    harness.advance_cycles(2);
  }

  // Restore quiet snapshot
  SsIoSpeaker_t quiet_state{};
  quiet_state.recently_active = 0;
  const PeripheralStatus_t status = speaker_get_descriptor()->load_state(
      instance, &quiet_state, sizeof(quiet_state));
  CHECK(status == peripheral_ok);

  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  REQUIRE_FALSE(harness.captured_samples().empty());
  for (int16_t s : harness.captured_samples()) {
    CHECK(s == 0);
  }
}

// =============================================================================
// Domain 7: Numerical Hardening, Stress Boundaries & Error Recovery
// =============================================================================

TEST_CASE(
    "Speaker Peripheral: Event Queue Saturation Under High-Frequency Stress") {
  // TC-19: Event Queue Saturation Under High-Frequency Stress
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  for (size_t i = 0; i < 20000; ++i) {
    harness.toggle_read();
    harness.advance_cycles(1);
  }
  harness.clear_captured_samples();
  harness.think(instance, 20000);
  REQUIRE_FALSE(harness.captured_samples().empty());

  const auto sat_min_max = std::minmax_element(
      harness.captured_samples().begin(), harness.captured_samples().end());
  CHECK(*sat_min_max.first < 0);
  CHECK(*sat_min_max.second > 0);
  // Anti-saturation check: verify waveform does not permanently rail at DAC
  // extremes
  CHECK(*sat_min_max.first > -32768);
  CHECK(*sat_min_max.second < 32767);
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
  CHECK(descriptor->query(instance, speaker_query_is_active, nullptr, &size) ==
        peripheral_ok);
  CHECK(size == sizeof(uint8_t));

  uint8_t active_dummy = 0;
  size_t small_size = 0;
  CHECK(descriptor->query(instance, speaker_query_is_active, &active_dummy,
                          &small_size) == peripheral_error);
  CHECK(descriptor->query(instance, speaker_query_is_active, &active_dummy,
                          nullptr) == peripheral_error);

  size = sizeof(uint8_t);
  CHECK(descriptor->query(instance, 0xFFFF, &active_dummy, &size) ==
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

  // Null instance handling across all API endpoints
  CHECK(descriptor->query(nullptr, speaker_query_is_active, &active_dummy,
                          &size) == peripheral_error);
  CHECK(descriptor->save_state(nullptr, tiny_buf, &size) == peripheral_error);
  CHECK(descriptor->load_state(nullptr, tiny_buf, sizeof(tiny_buf)) ==
        peripheral_error);
  descriptor->shutdown(nullptr);
  descriptor->reset(nullptr);
  descriptor->think(nullptr, 100);

  // Direct I/O with null instance
  const auto& handler = harness.get_handler(ADDR_SPEAKER);
  CHECK(handler.read(nullptr, 0, ADDR_SPEAKER, 0, 0x55, 0) ==
        mem_read_floating_bus(0));
  CHECK(handler.write(nullptr, 0, ADDR_SPEAKER, 1, 0x55, 0) ==
        mem_read_floating_bus(0));
}

TEST_CASE("Speaker Peripheral: Multiple Strobes in Identical Cycle") {
  // TC-23: Multiple Strobes in Identical Cycle (t1 == t2)
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(5000);
  harness.toggle_read();
  harness.toggle_read();  // Consecutive strobe at exact same cycle
  harness.advance_cycles(50);
  harness.think(instance, 50);
}

TEST_CASE("Speaker Peripheral: Immediate Silence on Active Reset") {
  // TC-24: Immediate Silence on Active Reset
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.toggle_read();
  CHECK(harness.is_active(instance));
  speaker_get_descriptor()->reset(instance);
  CHECK_FALSE(harness.is_active(instance));

  harness.clear_captured_samples();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);
  REQUIRE_FALSE(harness.captured_samples().empty());
  for (int16_t s : harness.captured_samples()) {
    CHECK(s == 0);
  }
}

// =============================================================================
// Domain 8: Intra-Frame Direct I/O Sub-Cycle Timing & Audio Synthesis
// =============================================================================

TEST_CASE(
    "Speaker Peripheral: Direct IO Bridge Sub-Cycle Cycle Synchronization") {
  // Verifies that when CPU instructions touch $C030 at sub-cycle offsets,
  // the Direct I/O bridge synchronizes CPU cumulative cycles so that
  // host->GetCycles() accurately reflects the instruction cycle rather than
  // stale frame-start cycles.
  ScopedCpuContext_t cpu_scope;
  linapple_init();
  peripheral_manager_init();
  REQUIRE(peripheral_register(speaker_get_descriptor(), 0) == 0);

  const uint64_t initial_cycles = cpu_get_cumulative_cycles();
  constexpr uint32_t instruction_cycle_offset = 512;

  // Simulate an instruction executing at sub-cycle offset 512 within the frame
  io_map_dispatch(0, ADDR_SPEAKER, 0, 0, instruction_cycle_offset);

  // The CPU cumulative cycles MUST advance to include the instruction cycle
  // offset
  CHECK(cpu_get_cumulative_cycles() ==
        initial_cycles + instruction_cycle_offset);

  linapple_shutdown();
}

TEST_CASE(
    "Speaker Peripheral: Intra-Frame 1 kHz Tone Synthesis via Direct IO "
    "Dispatch") {
  // Verifies that multiple speaker strobes distributed across a single 16.6ms
  // video frame (17,030 cycles) produce an active 1 kHz square-wave audio
  // signal rather than collapsing into sample 0 at the frame boundary (which
  // creates low-frequency 60 Hz bumps/mush).
  ScopedCpuContext_t cpu_scope;
  linapple_init();
  peripheral_manager_init();
  REQUIRE(peripheral_register(speaker_get_descriptor(), 0) == 0);

  static std::vector<int16_t> captured_frame_audio;
  captured_frame_audio.clear();

  g_frontendAudioCB = [](const int16_t* buffer, size_t count) {
    if (buffer != nullptr && count > 0) {
      captured_frame_audio.insert(captured_frame_audio.end(), buffer,
                                  buffer + count);
    }
  };

  // Simulate Apple II ROM BELL1 routine: ~1 kHz square wave (toggling every
  // ~1,000 cycles) across a standard 17,030-cycle frame (~16 strobes across the
  // frame)
  for (uint32_t cycle = 1000; cycle < 17000; cycle += 1000) {
    io_map_dispatch(0, ADDR_SPEAKER, 0, 0, cycle);
  }

  // Complete CPU execution for the frame
  cpu_calc_cycles(17030);

  // End of frame: peripheral manager thinks for 17,030 cycles
  peripheral_manager_think(17030);

  g_frontendAudioCB = nullptr;

  // At 44.1 kHz, 17,030 cycles generates 1,472 samples (736 stereo pairs)
  REQUIRE(captured_frame_audio.size() >= 1472);

  // Extract mono channel (left channel)
  std::vector<int16_t> mono;
  mono.reserve(captured_frame_audio.size() / 2);
  for (size_t i = 0; i < captured_frame_audio.size(); i += 2) {
    mono.push_back(captured_frame_audio[i]);
  }

  // Count zero crossings across the frame (samples 50 through 700)
  // With 16 toggles spaced ~43 samples apart, there must be distributed zero
  // crossings. In the buggy implementation, all toggles collapse into sample 0
  // and zero crossings between sample 50 and 700 is exactly 0.
  size_t zero_crossings = 0;
  for (size_t i = 50; i < 700 && i + 1 < mono.size(); ++i) {
    if ((mono[i] >= 0 && mono[i + 1] < 0) ||
        (mono[i] < 0 && mono[i + 1] >= 0)) {
      zero_crossings++;
    }
  }

  // Expect at least 10 zero crossings evenly distributed across the mid-frame
  // region
  CHECK(zero_crossings >= 10);

  linapple_shutdown();
}
