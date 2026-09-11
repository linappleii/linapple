// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "LinAppleCore.h"
#include "apple2/Apple2Types.h"
#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

namespace {

constexpr int DEFAULT_MOCKINGBOARD_SLOT = 4;
constexpr uint64_t INITIAL_MOCK_CYCLES = 10000;
constexpr int AY_MAX_VOLUME = 15;
constexpr int16_t AY_PEAK_AMPLITUDE_MAX_VOL = 18776;

class MockingboardHarness {
 public:
  MockingboardHarness() {
    s_active_harness = this;
    g_current_clk_6502 = CLOCK_6502;
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.GetConfig = Mock_GetConfig;
    host_.AudioPushSamples = Mock_AudioPushSamples;
    host_.GetCycles = Mock_GetCycles;
  }

  ~MockingboardHarness() {
    for (void* inst : instances_) {
      if (inst != nullptr) {
        mockingboard_get_descriptor()->shutdown(inst);
      }
    }
    instances_.clear();
    primary_instance_ = nullptr;
    s_active_harness = nullptr;
  }

  MockingboardHarness(const MockingboardHarness&) = delete;
  auto operator=(const MockingboardHarness&) -> MockingboardHarness& = delete;
  MockingboardHarness(MockingboardHarness&&) = delete;
  auto operator=(MockingboardHarness&&) -> MockingboardHarness& = delete;

  auto host() -> HostInterface_t* { return &host_; }
  auto descriptor() const -> Peripheral_t* {
    return mockingboard_get_descriptor();
  }

  auto create_card(int slot = DEFAULT_MOCKINGBOARD_SLOT) -> void* {
    void* instance = descriptor()->init(slot, &host_);
    if (instance != nullptr) {
      instances_.push_back(instance);
      if (primary_instance_ == nullptr) {
        primary_instance_ = instance;
        primary_slot_ = slot;
      }
      descriptor()->reset(instance);
    }
    return instance;
  }

  auto primary_instance() const -> void* { return primary_instance_; }

  auto reset(void* inst = nullptr) -> void {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (target != nullptr) {
      descriptor()->reset(target);
    }
  }

  auto think(uint32_t cycles = 0, void* inst = nullptr) -> void {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (target != nullptr) {
      descriptor()->think(target, cycles);
    }
  }

  auto save_state(void* buffer, size_t* size, void* inst = nullptr)
      -> PeripheralStatus_t {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (target == nullptr) {
      return peripheral_error;
    }
    return descriptor()->save_state(target, buffer, size);
  }

  auto load_state(const void* buffer, size_t size, void* inst = nullptr)
      -> PeripheralStatus_t {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (target == nullptr) {
      return peripheral_error;
    }
    return descriptor()->load_state(target, buffer, size);
  }

  auto read_c0(uint16_t addr, void* inst = nullptr) -> uint8_t {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (read_c0_handler_ != nullptr && target != nullptr) {
      return read_c0_handler_(target, 0, addr, 0, 0, 0);
    }
    return 0;
  }

  auto write_c0(uint16_t addr, uint8_t val, void* inst = nullptr) -> uint8_t {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (write_c0_handler_ != nullptr && target != nullptr) {
      return write_c0_handler_(target, 0, addr, 1, val, 0);
    }
    return 0;
  }

  auto read_cx(uint16_t addr, void* inst = nullptr) -> uint8_t {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (read_cx_handler_ != nullptr && target != nullptr) {
      return read_cx_handler_(target, 0, addr, 0, 0, 0);
    }
    return 0;
  }

  auto write_cx(uint16_t addr, uint8_t val, void* inst = nullptr) -> uint8_t {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (write_cx_handler_ != nullptr && target != nullptr) {
      return write_cx_handler_(target, 0, addr, 1, val, 0);
    }
    return 0;
  }

  auto has_read_c0() const -> bool { return read_c0_handler_ != nullptr; }
  auto has_write_c0() const -> bool { return write_c0_handler_ != nullptr; }
  auto has_read_cx() const -> bool { return read_cx_handler_ != nullptr; }
  auto has_write_cx() const -> bool { return write_cx_handler_ != nullptr; }

  auto cycles() const -> uint64_t { return cycles_; }
  auto set_cycles(uint64_t c) -> void { cycles_ = c; }
  auto advance_cycles(uint64_t delta) -> void { cycles_ += delta; }

  auto irq_asserted() const -> bool { return irq_asserted_; }
  auto irq_slot() const -> int { return irq_slot_; }

  auto set_config_type(const std::string& type) -> void { config_type_ = type; }

  auto audio_samples() const -> const std::vector<int16_t>& {
    return audio_samples_;
  }
  auto audio_push_call_count() const -> uint32_t {
    return audio_push_call_count_;
  }
  auto last_pushed_sample_count() const -> size_t {
    return last_pushed_sample_count_;
  }
  auto clear_audio() -> void {
    audio_samples_.clear();
    audio_push_call_count_ = 0;
    last_pushed_sample_count_ = 0;
  }

 private:
  static auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->irq_asserted_ = assert_irq;
      s_active_harness->irq_slot_ = slot;
    }
  }

  static auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                              PeripheralIOHandler write_c0,
                              PeripheralIOHandler read_cx,
                              PeripheralIOHandler write_cx) -> void {
    (void)slot;
    if (s_active_harness != nullptr) {
      s_active_harness->read_c0_handler_ = read_c0;
      s_active_harness->write_c0_handler_ = write_c0;
      s_active_harness->read_cx_handler_ = read_cx;
      s_active_harness->write_cx_handler_ = write_cx;
    }
  }

  static auto Mock_GetConfig(const char* section, const char* key, char* buffer,
                             size_t buffer_size) -> bool {
    if (s_active_harness == nullptr || buffer == nullptr || buffer_size == 0) {
      return false;
    }
    if (strcmp(section, "Mockingboard") == 0 && strcmp(key, "Type") == 0) {
      if (!s_active_harness->config_type_.empty()) {
        strncpy(buffer, s_active_harness->config_type_.c_str(), buffer_size);
        buffer[buffer_size - 1] = '\0';
        return true;
      }
    }
    return false;
  }

  static auto Mock_AudioPushSamples(void* instance, const int16_t* samples,
                                    size_t num_samples) -> void {
    (void)instance;
    if (s_active_harness != nullptr && samples != nullptr && num_samples > 0) {
      s_active_harness->audio_samples_.insert(
          s_active_harness->audio_samples_.end(), samples,
          samples + num_samples);
      s_active_harness->last_pushed_sample_count_ = num_samples;
      s_active_harness->audio_push_call_count_++;
    }
  }

  static auto Mock_GetCycles() -> uint64_t {
    if (s_active_harness != nullptr) {
      return s_active_harness->cycles_;
    }
    return 0;
  }

  static MockingboardHarness* s_active_harness;

  HostInterface_t host_{};
  uint64_t cycles_{INITIAL_MOCK_CYCLES};
  bool irq_asserted_{false};
  int irq_slot_{-1};
  std::string config_type_{};

  PeripheralIOHandler read_c0_handler_{nullptr};
  PeripheralIOHandler write_c0_handler_{nullptr};
  PeripheralIOHandler read_cx_handler_{nullptr};
  PeripheralIOHandler write_cx_handler_{nullptr};

  std::vector<int16_t> audio_samples_{};
  size_t last_pushed_sample_count_{0};
  uint32_t audio_push_call_count_{0};

  std::vector<void*> instances_{};
  void* primary_instance_{nullptr};
  int primary_slot_{DEFAULT_MOCKINGBOARD_SLOT};
};

MockingboardHarness* MockingboardHarness::s_active_harness = nullptr;

}  // namespace

TEST_CASE("Mockingboard Peripheral: Standard Mode") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  SUBCASE("Reset: Ensure IRQs are deasserted and registers are cleared") {
    harness.reset();
    CHECK_FALSE(harness.irq_asserted());
  }

  SUBCASE("State Persistence: Save and Load register state") {
    REQUIRE(harness.has_write_cx());
    REQUIRE(harness.has_read_cx());

    // Set a register in VIA A (e.g. DDRB at offset 2)
    harness.write_cx(0xC002, 0x55);

    size_t state_size = 0;
    CHECK(harness.save_state(nullptr, &state_size) == peripheral_ok);
    REQUIRE(state_size > 0);

    std::vector<uint8_t> buffer(state_size);
    REQUIRE(harness.save_state(buffer.data(), &state_size) == peripheral_ok);

    // Reset state and verify it's cleared
    harness.reset();
    CHECK(harness.read_cx(0xC002) == 0);

    // Load state and verify it's restored
    REQUIRE(harness.load_state(buffer.data(), state_size) == peripheral_ok);
    CHECK(harness.read_cx(0xC002) == 0x55);
  }

  SUBCASE("VIA Timer 1: One-shot IRQ timing and acknowledgment") {
    harness.reset();
    REQUIRE(harness.has_write_cx());

    // 1. Enable T1 interrupt in IER ($E) - Bit 6 + Bit 7 (SET bit)
    harness.write_cx(0xC00E, 0xC0);

    // 2. Set T1 period to 1000 cycles
    // T1L-L ($4) = 0xE8
    harness.write_cx(0xC004, 0xE8);
    // T1H-C ($5) = 0x03 (This starts the timer)
    harness.write_cx(0xC005, 0x03);

    CHECK_FALSE(harness.irq_asserted());

    // 3. Advance cycles partially
    harness.advance_cycles(500);
    harness.think(0);
    CHECK_FALSE(harness.irq_asserted());

    // 4. Advance cycles beyond period
    harness.advance_cycles(600);  // Total 1100 > 1000
    harness.think(0);
    CHECK(harness.irq_asserted());

    // 5. Clear IRQ by reading T1L-L ($4)
    harness.read_cx(0xC004);
    harness.think(0);
    CHECK_FALSE(harness.irq_asserted());
  }

  SUBCASE("VIA Timer 1: Continuous mode periodicity") {
    harness.reset();
    REQUIRE(harness.has_write_cx());

    // Set Continuous Mode in ACR (Bit 6 = 1)
    harness.write_cx(0xC00B, 0x40);
    // Enable T1 IRQ
    harness.write_cx(0xC00E, 0xC0);

    // Set period to 500 cycles (> 255 limitation)
    harness.write_cx(0xC004, 0xF4);  // 500 & 0xFF = 0xF4
    harness.write_cx(0xC005, 0x01);  // 500 >> 8 = 1

    // First underflow
    harness.advance_cycles(501);
    harness.think(0);
    CHECK(harness.irq_asserted());

    // Ack IRQ
    harness.read_cx(0xC004);
    harness.think(0);
    CHECK_FALSE(harness.irq_asserted());

    // Second underflow (continuous mode should reload)
    harness.advance_cycles(501);
    harness.think(0);
    CHECK(harness.irq_asserted());
  }

  SUBCASE("AY-3-8910: Complex interaction via VIA registers") {
    // Set DDRs to output
    harness.write_cx(0xC002, 0xFF);  // DDRB
    harness.write_cx(0xC003, 0xFF);  // DDRA

    // Reset chips (Bit 2 of ORB high)
    harness.write_cx(0xC000, 0x04);

    // 1. Latch AY register 7 (Mixer)
    // ORA = 7
    harness.write_cx(0xC001, 0x07);
    // ORB = func_latch (BDIR=1, BC1=1) + RESET_N=1 -> 0x03 | 0x04 = 0x07
    harness.write_cx(0xC000, 0x07);
    // ORB = Inactive (BDIR=0, BC1=0) + RESET_N=1 -> 0x04
    harness.write_cx(0xC000, 0x04);

    // 2. Write 0x3F to Mixer
    // ORA = 0x3F
    harness.write_cx(0xC001, 0x3F);
    // ORB = func_write (BDIR=1, BC1=0) + RESET_N=1 -> 0x02 | 0x04 = 0x06
    harness.write_cx(0xC000, 0x06);
    // ORB = Inactive
    harness.write_cx(0xC000, 0x04);

    // 3. Verify AY register persists through state cycle
    size_t state_size = 0;
    CHECK(harness.save_state(nullptr, &state_size) == peripheral_ok);
    std::vector<uint8_t> buffer(state_size);
    CHECK(harness.save_state(buffer.data(), &state_size) == peripheral_ok);

    harness.reset();
    REQUIRE(harness.load_state(buffer.data(), state_size) == peripheral_ok);
  }

  SUBCASE("VIA Timer 1: Worst-case timer period bounds audio buffer (TASK-4)") {
    harness.reset();
    REQUIRE(harness.has_write_cx());

    // 1. Enable T1 interrupt in IER ($E) - Bit 6 + Bit 7 (SET bit)
    harness.write_cx(0xC00E, 0xC0);

    // 2. Set T1 period to maximum 16-bit value (0xFFFF = 65535 cycles)
    // T1L-L ($4) = 0xFF
    harness.write_cx(0xC004, 0xFF);
    // T1H-C ($5) = 0xFF (Starts timer with period 65535)
    harness.write_cx(0xC005, 0xFF);

    // Process register access to activate card without advancing cycles
    harness.think(0);

    CHECK_FALSE(harness.irq_asserted());
    harness.clear_audio();

    // 3. Advance cycles to trigger T1 underflow (65535 + 1)
    harness.advance_cycles(65536);
    harness.think(0);

    // Underflow asserts IRQ and emits exactly 2832 mono / 5664 stereo samples
    CHECK(harness.irq_asserted());
    CHECK(harness.audio_push_call_count() == 1);
    CHECK(harness.last_pushed_sample_count() == 5664);
    REQUIRE(harness.audio_samples().size() == 5664);

    // Deep audio assertion: When AY tone generators are unconfigured / muted,
    // all samples must be absolute silence (amplitude == 0).
    bool all_silent = true;
    for (int16_t sample : harness.audio_samples()) {
      if (sample != 0) {
        all_silent = false;
        break;
      }
    }
    CHECK(all_silent);
  }

  SUBCASE(
      "Audio Synthesis: Synthesized square wave tone when AY is active, "
      "silence when muted") {
    harness.reset();
    REQUIRE(harness.has_write_cx());

    // Configure VIA Port A and B for AY control
    harness.write_cx(0xC002, 0xFF);  // DDRB output
    harness.write_cx(0xC003, 0xFF);  // DDRA output
    harness.write_cx(0xC000, 0x04);  // RESET_N high, bus inactive

    // Helper to write AY register via VIA bus handshake lines
    auto write_ay = [&harness](uint8_t reg, uint8_t val) {
      // Latch register
      harness.write_cx(0xC001, reg);
      harness.write_cx(0xC000, 0x07);  // BDIR=1, BC1=1 (func_latch) + RESET_N
      harness.write_cx(0xC000, 0x04);  // Inactive + RESET_N
      // Write data
      harness.write_cx(0xC001, val);
      harness.write_cx(0xC000, 0x06);  // BDIR=1, BC1=0 (func_write) + RESET_N
      harness.write_cx(0xC000, 0x04);  // Inactive + RESET_N
    };

    // 1. Set Channel A tone period: Fine tune = 20, Coarse = 0
    write_ay(0x00, 20);
    write_ay(0x01, 0x00);

    // 2. Set Channel A amplitude to maximum volume (15)
    write_ay(0x08, AY_MAX_VOLUME);

    // 3. Set Mixer (Reg 7): Enable Tone A (bit 0 = 0), disable others (0x3E)
    write_ay(0x07, 0x3E);

    // 4. Configure VIA Timer 1 to trigger periodic audio generation
    // Period = 1000 cycles
    harness.write_cx(0xC00E, 0xC0);  // Enable T1 IRQ
    harness.write_cx(0xC004, 0xE8);  // 1000 & 0xFF
    harness.write_cx(0xC005, 0x03);  // 1000 >> 8

    harness.clear_audio();

    // Advance cycles to trigger T1 underflow and push audio
    harness.advance_cycles(1001);
    harness.think(0);

    CHECK(harness.irq_asserted());
    CHECK(harness.audio_push_call_count() == 1);
    REQUIRE_FALSE(harness.audio_samples().empty());

    // Deep Audio Verification:
    // With Channel A tone active at volume 15 on Chip A (Left channel):
    // - Left channel samples alternate between 0 and 18776 (square wave).
    // - Right channel samples (Chip B) should remain silent (0).
    // - Peak amplitude on left channel must equal 18776.
    // - Left channel must contain both high and low phases of the tone.
    int16_t peak_left = 0;
    int16_t min_left = 0;
    size_t high_samples_left = 0;
    size_t low_samples_left = 0;
    bool right_channel_silent = true;

    const auto& samples = harness.audio_samples();
    const size_t num_frames = samples.size() / 2;
    REQUIRE(num_frames > 0);

    for (size_t i = 0; i < num_frames; ++i) {
      const int16_t left = samples[i * 2];
      const int16_t right = samples[i * 2 + 1];

      if (left > peak_left) {
        peak_left = left;
      }
      if (left < min_left) {
        min_left = left;
      }
      if (left == AY_PEAK_AMPLITUDE_MAX_VOL) {
        high_samples_left++;
      } else if (left == 0) {
        low_samples_left++;
      }
      if (right != 0) {
        right_channel_silent = false;
      }
    }

    CHECK(peak_left == AY_PEAK_AMPLITUDE_MAX_VOL);
    CHECK(min_left == 0);
    CHECK(high_samples_left > 0);
    CHECK(low_samples_left > 0);
    CHECK(high_samples_left + low_samples_left == num_frames);
    CHECK(right_channel_silent);

    // 5. Mute Channel A by setting Volume to 0
    write_ay(0x08, 0x00);
    harness.read_cx(0xC004);  // Ack IRQ
    harness.clear_audio();

    // Re-arm timer for next period
    harness.write_cx(0xC005, 0x03);
    harness.advance_cycles(1001);
    harness.think(0);

    CHECK(harness.audio_push_call_count() == 1);
    REQUIRE_FALSE(harness.audio_samples().empty());

    bool muted_all_silent = true;
    for (int16_t sample : harness.audio_samples()) {
      if (sample != 0) {
        muted_all_silent = false;
        break;
      }
    }
    CHECK(muted_all_silent);
  }
}

TEST_CASE("Mockingboard Peripheral: Phasor Card Mode") {
  MockingboardHarness harness;
  harness.set_config_type("Phasor");
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  SUBCASE("Phasor: Native Mode Detection and chip selection") {
    REQUIRE(harness.has_read_c0());
    // Access C0nX range (Phasor native select)
    // addr = $C0C1 (bit 0 high triggers native mode)
    harness.read_c0(0xC0C1);

    // In native mode, addr bit 3 and 2 select the chip
    // cs = ((addr & 0x08) >> 2) | ((addr & 0x04) >> 2)
    // Select Chip A via bit 2 (cs=1). Use register 6 (T1L-H).
    harness.write_c0(0xC0C6, 0xAA);
    CHECK(harness.read_c0(0xC0C6) == 0xAA);

    // Select Chip B via bit 3 (cs=2). Use register 0xA (SR).
    harness.write_c0(0xC0CA, 0xBB);
    CHECK(harness.read_c0(0xC0CA) == 0xBB);
  }

  SUBCASE("Phasor: State preservation including card type") {
    // Set native mode
    harness.read_c0(0xC0C1);
    // Write something in native mode
    harness.write_c0(0xC0C6, 0xAA);

    size_t state_size = 0;
    CHECK(harness.save_state(nullptr, &state_size) == peripheral_ok);
    REQUIRE(state_size > 0);
    std::vector<uint8_t> buffer(state_size);
    CHECK(harness.save_state(buffer.data(), &state_size) == peripheral_ok);

    harness.reset();
    // After reset, native mode should be false
    // Write something else to Chip B (which C0C6 maps to when NOT in native
    // mode)
    harness.write_c0(0xC0C6, 0xEE);

    REQUIRE(harness.load_state(buffer.data(), state_size) == peripheral_ok);

    // Verify native mode restored by checking chip selection
    // Reg 6 of Chip A should have what we wrote in native mode
    CHECK(harness.read_c0(0xC0C6) == 0xAA);
  }
}

TEST_CASE("Mockingboard Peripheral: [MB-19] Independent instance allocation") {
  MockingboardHarness harness;
  void* instance1 = harness.create_card(4);
  REQUIRE(instance1 != nullptr);

  void* instance2 = harness.create_card(5);
  REQUIRE(instance2 != nullptr);
  CHECK(instance1 != instance2);
}
