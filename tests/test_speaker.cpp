// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
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

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

constexpr uint16_t ADDR_SPEAKER = 0xC030;
constexpr int TEST_SLOT = 0;
constexpr int16_t SPEAKER_PEAK_AMPLITUDE = 0x4000;
constexpr uint64_t INACTIVITY_THRESHOLD_CYCLES =
    static_cast<uint64_t>(CLOCK_6502 / 5.0);

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

  auto set_host_full_speed(bool fs) -> void { host_full_speed_ = fs; }
  auto set_drop_audio(bool drop) -> void {
    host_.AudioPushSamples = drop ? nullptr : mock_audio_push_samples;
  }

  auto has_handler(uint16_t addr) const -> bool {
    return handlers_.find(addr) != handlers_.end();
  }

  auto get_handler(uint16_t addr) const -> const MockDirectIOHandler_t& {
    return handlers_.at(addr);
  }

  auto read_io(uint16_t addr, uint8_t floating_bus = 0) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.read != nullptr) {
      void* target = (it->second.instance != nullptr) ? it->second.instance
                                                      : primary_instance_;
      return it->second.read(target, 0, addr, 0, floating_bus, 0);
    }
    return floating_bus;
  }

  auto write_io(uint16_t addr, uint8_t val) -> uint8_t {
    auto it = handlers_.find(addr);
    if (it != handlers_.end() && it->second.write != nullptr) {
      void* target = (it->second.instance != nullptr) ? it->second.instance
                                                      : primary_instance_;
      return it->second.write(target, 0, addr, 1, val, 0);
    }
    return 0;
  }

  auto toggle_read(uint8_t floating_bus = 0) -> uint8_t {
    return read_io(ADDR_SPEAKER, floating_bus);
  }

  auto toggle_write(uint8_t val = 0) -> uint8_t {
    return write_io(ADDR_SPEAKER, val);
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
  bool host_full_speed_ = false;

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
      if (s_active_harness->host_full_speed_) {
        return;
      }
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

TEST_CASE("Speaker Peripheral: Registration and Lifecycle") {
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

  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  CHECK(harness.has_handler(ADDR_SPEAKER));
  const auto& handler = harness.get_handler(ADDR_SPEAKER);
  CHECK(handler.read != nullptr);
  CHECK(handler.write != nullptr);
  CHECK(handler.instance == instance);

  descriptor->reset(instance);
  CHECK_FALSE(harness.is_active(instance));

  harness.shutdown_instance(instance);
}

TEST_CASE("Speaker Peripheral: Direct IO and Physical Toggle") {
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(1000);
  CHECK_FALSE(harness.is_active(instance));

  // Direct I/O Read must return floating bus noise and latch toggle
  const uint8_t read_res_1 = harness.toggle_read();
  CHECK(read_res_1 == mem_read_floating_bus(0));
  CHECK(harness.is_active(instance));

  // Direct I/O Write must toggle and maintain activity
  harness.advance_cycles(50);
  constexpr uint8_t test_write_val = 0xA5;
  const uint8_t write_res = harness.toggle_write(test_write_val);
  CHECK(write_res == mem_read_floating_bus(0));
  CHECK(harness.is_active(instance));

  // Direct I/O Read with subsequent cycle
  harness.advance_cycles(50);
  const uint8_t read_res_2 = harness.toggle_read();
  CHECK(read_res_2 == mem_read_floating_bus(0));
}

TEST_CASE("Speaker Peripheral: Audio Generation and Filtering") {
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // 1. Silent generation when inactive
  harness.set_cycles(1000);
  harness.think(instance, 1000);

  REQUIRE_FALSE(harness.captured_samples().empty());
  for (int16_t s : harness.captured_samples()) {
    CHECK(s == 0);
  }
  harness.clear_captured_samples();

  // 2. Toggle speaker and verify deterministic impulse step response
  harness.toggle_read();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);

  const auto& samples = harness.captured_samples();
  REQUIRE_FALSE(samples.empty());
  REQUIRE(samples.size() % 2 == 0);

  // Interleaved stereo samples: left and right channels must match exactly
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

  // 3. DC blocking filter decay towards silence over long duration
  harness.clear_captured_samples();
  constexpr uint32_t long_wait_cycles = 2000000;
  harness.advance_cycles(long_wait_cycles);
  harness.think(instance, long_wait_cycles);

  REQUIRE_FALSE(harness.captured_samples().empty());
  CHECK(harness.captured_samples().back() == 0);
}

TEST_CASE("Speaker Peripheral: Inactivity Tracking") {
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

TEST_CASE("Speaker Peripheral: Host Full-Speed Suppression") {
  SpeakerHarness_t harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_host_full_speed(true);
  harness.toggle_read();
  harness.advance_cycles(1000);
  harness.think(instance, 1000);

  CHECK(harness.captured_samples().empty());
  CHECK(harness.audio_push_count() == 0);

  // Restoring normal speed permits audio buffer delivery
  harness.set_host_full_speed(false);
  harness.advance_cycles(1000);
  harness.think(instance, 1000);

  CHECK_FALSE(harness.captured_samples().empty());
  CHECK(harness.audio_push_count() == 1);
}

TEST_CASE("Speaker Peripheral: State Persistence Integrity") {
  SpeakerHarness_t harness;
  void* instance1 = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance1 != nullptr);

  harness.set_cycles(1000);
  harness.toggle_read();
  harness.think(instance1, 0);
  REQUIRE(harness.is_active(instance1));

  // Two-pass save state contract
  size_t state_size = 0;
  PeripheralStatus_t status =
      speaker_get_descriptor()->save_state(instance1, nullptr, &state_size);
  CHECK(status == peripheral_ok);
  CHECK(state_size == sizeof(SsIoSpeaker_t));

  std::vector<uint8_t> buffer(state_size);
  status = speaker_get_descriptor()->save_state(instance1, buffer.data(),
                                                &state_size);
  CHECK(status == peripheral_ok);

  void* instance2 = harness.create_speaker(TEST_SLOT + 1);
  REQUIRE(instance2 != nullptr);
  CHECK_FALSE(harness.is_active(instance2));

  // Restore state into second instance
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

TEST_CASE("Speaker Peripheral: Robustness and Numerical Guards") {
  auto* descriptor = speaker_get_descriptor();
  SpeakerHarness_t harness;

  // 1. Null host on initialization
  CHECK(descriptor->init(TEST_SLOT, nullptr) == nullptr);

  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // 2. Query ABI validation
  size_t size = 0;
  CHECK(descriptor->query(instance, speaker_query_is_active, nullptr, &size) ==
        peripheral_ok);
  CHECK(size == sizeof(uint8_t));

  uint8_t active_dummy = 0;
  size_t small_size = 0;
  CHECK(descriptor->query(instance, speaker_query_is_active, &active_dummy,
                          &small_size) == peripheral_error);

  size = sizeof(uint8_t);
  CHECK(descriptor->query(instance, 0xFFFF, &active_dummy, &size) ==
        peripheral_incompatible);

  // 3. Save / Load state sizing and corruption guards
  size = 0;
  CHECK(descriptor->save_state(instance, nullptr, &size) == peripheral_ok);
  CHECK(size == sizeof(SsIoSpeaker_t));

  uint8_t tiny_buf[1] = {0};
  size = sizeof(tiny_buf);
  CHECK(descriptor->save_state(instance, tiny_buf, &size) == peripheral_error);

  CHECK(descriptor->load_state(instance, tiny_buf, sizeof(tiny_buf)) ==
        peripheral_error);
  CHECK(descriptor->load_state(instance, nullptr, sizeof(SsIoSpeaker_t)) ==
        peripheral_error);

  // Oversized buffer must be strictly rejected
  std::vector<uint8_t> oversized_buf(sizeof(SsIoSpeaker_t) + 16, 0);
  CHECK(descriptor->load_state(instance, oversized_buf.data(),
                               oversized_buf.size()) == peripheral_error);

  // Corrupted save state (NaN/Inf in filter_state)
  SsIoSpeaker_t corrupted_state{};
  corrupted_state.filter_state = NAN;
  corrupted_state.next_sample_cycle = -100.0;
  CHECK(descriptor->load_state(instance, &corrupted_state,
                               sizeof(SsIoSpeaker_t)) == peripheral_ok);

  // 4. Null instance handling across all API functions
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

  // 5. Backward clock / cycle underflow safety (end_cycle < elapsed_cycles)
  harness.set_cycles(10);
  harness.think(instance, 1000);  // 10 < 1000: start_cycle clamped to 0

  // 6. High-frequency toggle saturation: toggle 20,000 times (> 16384 capacity)
  for (size_t i = 0; i < 20000; ++i) {
    harness.toggle_read();
    harness.advance_cycles(1);
  }
  harness.clear_captured_samples();
  harness.think(instance, 20000);
  REQUIRE_FALSE(harness.captured_samples().empty());

  // Verify high-frequency toggle saturation generates active, non-zero bipolar
  // waveform
  const auto min_max = std::minmax_element(harness.captured_samples().begin(),
                                           harness.captured_samples().end());
  CHECK(*min_max.first < 0);
  CHECK(*min_max.second > 0);

  // 7. Null AudioPushSamples host callback safety
  harness.set_drop_audio(true);
  harness.toggle_read();
  harness.advance_cycles(500);
  harness.think(instance, 500);  // Must not crash with null AudioPushSamples
  harness.set_drop_audio(false);

  // 8. Invariant reset clearing
  harness.toggle_read();
  CHECK(harness.is_active(instance));
  descriptor->reset(instance);
  CHECK_FALSE(harness.is_active(instance));
}
