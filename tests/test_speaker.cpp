// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

extern "C" auto video_get_scanner_address(uint32_t*, uint32_t) -> uint16_t {
  return 0;
}

namespace {

constexpr uint16_t ADDR_SPEAKER = 0xC030;
constexpr int TEST_SLOT = 0;

constexpr uint16_t IO_BASE_ADDRESS = 0xC080;
constexpr int IO_SLOT_OFFSET = 4;
constexpr int REGISTERS_PER_SLOT = 16;

constexpr double STANDARD_APPLE2_SPEED = CLOCK_6502;

constexpr uint64_t CYCLES_INITIAL = 1000;
constexpr uint32_t CYCLES_WAIT_LONG = 2000000;
constexpr double TIMEOUT_DIVISOR_HALF = 2.0;

constexpr int16_t DC_BLOCK_THRESHOLD = 100;
constexpr size_t MEMORY_SIZE_64K = 65536;

struct MockHandler_t {
  void* instance = nullptr;
  PeripheralIOHandler read = nullptr;
  PeripheralIOHandler write = nullptr;

  MockHandler_t() = default;
  MockHandler_t(void* inst, PeripheralIOHandler r, PeripheralIOHandler w)
      : instance(inst), read(r), write(w) {}
};

class SpeakerHarness {
 public:
  SpeakerHarness() {
    s_active_harness = this;
    scoped_mem_.fill(0);
    prev_mem_ = mem;
    mem = scoped_mem_.data();

    prev_cycles_ = g_cumulative_cycles;
    prev_clk_ = g_current_clk_6502;
    prev_full_speed_ = g_full_speed;

    cycles_ = 0;
    g_cumulative_cycles = 0;
    g_current_clk_6502 = STANDARD_APPLE2_SPEED;
    g_full_speed = false;

    host_.Log = Mock_Log;
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.RegisterCxROM = Mock_RegisterCxROM;
    host_.RegisterExpansionROM = Mock_RegisterExpansionROM;
    host_.RegisterDirectIO = Mock_RegisterDirectIO;
    host_.AudioPushSamples = Mock_AudioPushSamples;
    host_.GetCycles = Mock_GetCycles;
  }

  ~SpeakerHarness() {
    for (void* inst : instances_) {
      if (inst != nullptr) {
        speaker_get_descriptor()->shutdown(inst);
      }
    }
    instances_.clear();
    primary_instance_ = nullptr;
    handlers_.clear();
    captured_samples_.clear();

    mem = prev_mem_;
    g_cumulative_cycles = prev_cycles_;
    g_current_clk_6502 = prev_clk_;
    g_full_speed = prev_full_speed_;
    s_active_harness = nullptr;
  }

  SpeakerHarness(const SpeakerHarness&) = delete;
  auto operator=(const SpeakerHarness&) -> SpeakerHarness& = delete;
  SpeakerHarness(SpeakerHarness&&) = delete;
  auto operator=(SpeakerHarness&&) -> SpeakerHarness& = delete;

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
  auto set_cycles(uint64_t c) -> void {
    cycles_ = c;
    g_cumulative_cycles = c;
  }
  auto advance_cycles(uint64_t delta) -> void {
    cycles_ += delta;
    g_cumulative_cycles += delta;
  }

  auto set_full_speed(bool fs) -> void { g_full_speed = fs; }

  auto has_handler(uint16_t addr) const -> bool {
    return handlers_.find(addr) != handlers_.end();
  }

  auto get_handler(uint16_t addr) const -> const MockHandler_t& {
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

  auto toggle_read() -> uint8_t { return read_io(ADDR_SPEAKER); }
  auto toggle_write(uint8_t val = 0) -> uint8_t {
    return write_io(ADDR_SPEAKER, val);
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
    bool active = false;
    size_t size = sizeof(active);
    PeripheralStatus_t status = speaker_get_descriptor()->query(
        target, speaker_query_is_active, &active, &size);
    if (status != peripheral_ok) {
      return false;
    }
    return active;
  }

 private:
  std::array<uint8_t, MEMORY_SIZE_64K> scoped_mem_{};
  uint8_t* prev_mem_ = nullptr;
  uint64_t prev_cycles_ = 0;
  double prev_clk_ = 0.0;
  bool prev_full_speed_ = false;

  HostInterface_t host_{};
  uint64_t cycles_ = 0;
  std::vector<void*> instances_;
  void* primary_instance_ = nullptr;
  std::map<uint16_t, MockHandler_t> handlers_;
  std::vector<int16_t> captured_samples_;
  uint32_t audio_push_count_ = 0;

  static SpeakerHarness* s_active_harness;

  static auto Mock_Log(void* instance, PeripheralLogLevel_t level,
                       const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    (void)fmt;
  }

  static auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
    (void)slot;
    (void)assert_irq;
  }

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  // Justification: Signature required by HostInterface_t ABI.
  static auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                              PeripheralIOHandler write_c0,
                              PeripheralIOHandler read_cx,
                              PeripheralIOHandler write_cx) -> void {
    (void)slot;
    (void)read_cx;
    (void)write_cx;
    if (s_active_harness != nullptr &&
        (read_c0 != nullptr || write_c0 != nullptr)) {
      const uint16_t base = IO_BASE_ADDRESS + (slot << IO_SLOT_OFFSET);
      for (uint16_t i = 0; i < REGISTERS_PER_SLOT; ++i) {
        s_active_harness->handlers_[base + i] = {nullptr, read_c0, write_c0};
      }
    }
  }

  static auto Mock_RegisterCxROM(int slot, uint8_t* rom_ptr) -> void {
    (void)slot;
    (void)rom_ptr;
  }

  static auto Mock_RegisterExpansionROM(int slot, uint8_t* rom_ptr) -> void {
    (void)slot;
    (void)rom_ptr;
  }

  static auto Mock_RegisterDirectIO(void* instance, uint16_t addr,
                                    PeripheralIOHandler read,
                                    PeripheralIOHandler write) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->handlers_[addr] = {instance, read, write};
    }
  }

  static auto Mock_AudioPushSamples(void* instance, const int16_t* buffer,
                                    size_t num_samples) -> void {
    (void)instance;
    if (s_active_harness != nullptr && buffer != nullptr && num_samples > 0) {
      s_active_harness->captured_samples_.insert(
          s_active_harness->captured_samples_.end(), buffer,
          buffer + num_samples);
      s_active_harness->audio_push_count_++;
    }
  }

  static auto Mock_GetCycles() -> uint64_t {
    if (s_active_harness != nullptr) {
      return s_active_harness->cycles_;
    }
    return 0;
  }
  // NOLINTEND(bugprone-easily-swappable-parameters)
};

SpeakerHarness* SpeakerHarness::s_active_harness = nullptr;

}  // namespace

TEST_CASE("Speaker Peripheral: Registration and Lifecycle") {
  SpeakerHarness harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  CHECK(harness.has_handler(ADDR_SPEAKER));
  const auto& handler = harness.get_handler(ADDR_SPEAKER);
  CHECK(handler.read != nullptr);
  CHECK(handler.write != nullptr);
  CHECK(handler.instance == instance);

  harness.shutdown_instance(instance);
}

TEST_CASE("Speaker Peripheral: Toggle and Event Tracking") {
  SpeakerHarness harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(CYCLES_INITIAL);

  // Read toggle
  harness.toggle_read();

  std::array<SpeakerEvent_t, max_speaker_events> events{};
  uint32_t count =
      speaker_get_events(instance, events.data(), max_speaker_events);
  CHECK(count == 1);
  CHECK(events.at(0).cycle == CYCLES_INITIAL);
  CHECK(events.at(0).state == true);

  // Subsequent call yields zero (drained)
  CHECK(speaker_get_events(instance, events.data(), max_speaker_events) == 0);

  // Write toggle at next cycle
  const uint64_t write_cycle = CYCLES_INITIAL + 50;
  harness.set_cycles(write_cycle);
  harness.toggle_write(0xAA);

  count = speaker_get_events(instance, events.data(), max_speaker_events);
  CHECK(count == 1);
  CHECK(events.at(0).cycle == write_cycle);
  CHECK(events.at(0).state == false);
}

TEST_CASE("Speaker Peripheral: Audio Generation and Filtering") {
  SpeakerHarness harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // 1. Silence when inactive
  harness.set_cycles(CYCLES_INITIAL);
  speaker_generate_samples(instance, static_cast<uint32_t>(CYCLES_INITIAL));

  REQUIRE_FALSE(harness.captured_samples().empty());
  for (int16_t s : harness.captured_samples()) {
    CHECK(s == 0);
  }
  harness.clear_captured_samples();

  // 2. Toggle speaker and generate step response
  harness.toggle_read();
  harness.advance_cycles(CYCLES_INITIAL);
  speaker_generate_samples(instance, static_cast<uint32_t>(CYCLES_INITIAL));

  const auto& samples = harness.captured_samples();
  REQUIRE_FALSE(samples.empty());
  REQUIRE(samples.size() % 2 == 0);

  // Interleaved stereo samples: left and right channels must match exactly
  for (size_t i = 0; i < samples.size(); i += 2) {
    CHECK(samples[i] == samples[i + 1]);
  }

  // Exact golden step response checks:
  // First sample pair after step transition reaches peak amplitude
  CHECK(samples[0] == speaker_sample_volume);
  CHECK(samples[1] == speaker_sample_volume);

  // Consecutive samples decay via DC blocker filter (factor 0.999f per sample)
  constexpr float dc_coeff = 0.999f;
  const auto expected_sample_2 =
      static_cast<int16_t>(dc_coeff * speaker_sample_volume);
  CHECK(samples[2] == expected_sample_2);
  CHECK(samples[3] == expected_sample_2);

  const auto expected_sample_4 =
      static_cast<int16_t>(dc_coeff * dc_coeff * speaker_sample_volume);
  CHECK(samples[4] == expected_sample_4);
  CHECK(samples[5] == expected_sample_4);

  // Peak amplitude verification: maximum positive value is exactly
  // speaker_sample_volume
  int16_t max_sample = -32768;
  int16_t min_sample = 32767;
  for (int16_t s : samples) {
    if (s > max_sample) {
      max_sample = s;
    }
    if (s < min_sample) {
      min_sample = s;
    }
  }
  CHECK(max_sample == speaker_sample_volume);
  CHECK(min_sample > 0);
  CHECK(samples.front() > samples.back());

  // 3. DC blocking filter decay towards zero over long cycle interval
  harness.clear_captured_samples();
  harness.advance_cycles(CYCLES_WAIT_LONG);
  speaker_generate_samples(instance, CYCLES_WAIT_LONG);

  REQUIRE_FALSE(harness.captured_samples().empty());
  const int16_t last_sample = harness.captured_samples().back();
  CHECK(std::abs(last_sample) < DC_BLOCK_THRESHOLD);
  CHECK(std::abs(last_sample) <= 10);
}

TEST_CASE("Speaker Peripheral: Inactivity Tracking") {
  SpeakerHarness harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_cycles(CYCLES_INITIAL);
  CHECK_FALSE(harness.is_active(instance));

  harness.toggle_read();
  speaker_get_descriptor()->think(instance, 0);
  CHECK(harness.is_active(instance));

  // The inactivity threshold is clk / 5 cycles
  const auto timeout_cycles =
      static_cast<uint32_t>(STANDARD_APPLE2_SPEED / TIMEOUT_DIVISOR_HALF);
  harness.advance_cycles(timeout_cycles);
  speaker_get_descriptor()->think(instance, timeout_cycles);

  CHECK_FALSE(harness.is_active(instance));
}

TEST_CASE("Speaker Peripheral: Full-Speed Suppression") {
  SpeakerHarness harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  harness.set_full_speed(true);

  harness.toggle_read();

  CHECK_FALSE(harness.is_active(instance));

  std::array<SpeakerEvent_t, max_speaker_events> events{};
  CHECK(speaker_get_events(instance, events.data(), max_speaker_events) == 0);
}

TEST_CASE("Speaker Peripheral: State Persistence Integrity") {
  SpeakerHarness harness;
  void* instance1 = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance1 != nullptr);

  harness.set_cycles(CYCLES_INITIAL);

  harness.toggle_read();
  speaker_get_descriptor()->think(instance1, 0);
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

TEST_CASE("Speaker Peripheral: Event Buffer Limits") {
  SpeakerHarness harness;
  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  for (size_t i = 0; i < max_speaker_events; ++i) {
    harness.toggle_read();
    harness.advance_cycles(1);
  }

  // One more toggle beyond max_speaker_events capacity
  harness.toggle_read();

  std::array<SpeakerEvent_t, max_speaker_events> events{};
  uint32_t count =
      speaker_get_events(instance, events.data(), max_speaker_events);
  CHECK(count == max_speaker_events);
}

TEST_CASE("Speaker Peripheral: Robustness and ABI") {
  SpeakerHarness harness;

  CHECK(speaker_get_descriptor()->init(TEST_SLOT, nullptr) == nullptr);

  void* instance = harness.create_speaker(TEST_SLOT);
  REQUIRE(instance != nullptr);

  // Query size retrieval
  size_t size = 0;
  CHECK(speaker_get_descriptor()->query(instance, speaker_query_is_active,
                                        nullptr, &size) == peripheral_ok);
  CHECK(size == sizeof(bool));

  // Query buffer too small
  bool active_dummy = false;
  size_t small_size = 0;
  CHECK(speaker_get_descriptor()->query(instance, speaker_query_is_active,
                                        &active_dummy,
                                        &small_size) == peripheral_error);

  // Invalid query ID
  size = sizeof(bool);
  CHECK(speaker_get_descriptor()->query(instance, 0xFFFF, &active_dummy,
                                        &size) == peripheral_incompatible);

  // Save state size retrieval
  size = 0;
  CHECK(speaker_get_descriptor()->save_state(instance, nullptr, &size) ==
        peripheral_ok);
  CHECK(size == sizeof(SsIoSpeaker_t));

  // Save state buffer too small
  uint8_t tiny_buf[1] = {0};
  size = sizeof(tiny_buf);
  CHECK(speaker_get_descriptor()->save_state(instance, tiny_buf, &size) ==
        peripheral_error);

  // Load state buffer too small
  CHECK(speaker_get_descriptor()->load_state(
            instance, tiny_buf, sizeof(tiny_buf)) == peripheral_error);

  // Load state null buffer
  CHECK(speaker_get_descriptor()->load_state(
            instance, nullptr, sizeof(SsIoSpeaker_t)) == peripheral_error);

  // Query/Save/Load with null instance
  CHECK(speaker_get_descriptor()->query(nullptr, speaker_query_is_active,
                                        &active_dummy,
                                        &size) == peripheral_error);
  CHECK(speaker_get_descriptor()->save_state(nullptr, tiny_buf, &size) ==
        peripheral_error);
  CHECK(speaker_get_descriptor()->load_state(
            nullptr, tiny_buf, sizeof(SsIoSpeaker_t)) == peripheral_error);

  // Public synthesis API null checks
  CHECK(speaker_get_events(nullptr, nullptr, 0) == 0);
  CHECK(speaker_get_last_cycle(nullptr) == 0);
  speaker_generate_samples(nullptr, 100);
}
