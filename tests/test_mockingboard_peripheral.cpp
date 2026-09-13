// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "apple2/peripherals/mockingboard/MockingboardCommands.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

extern bool g_full_speed;

namespace {

constexpr int DEFAULT_MOCKINGBOARD_SLOT = 4;
constexpr uint64_t INITIAL_MOCK_CYCLES = 10000;
constexpr int AY_MAX_VOLUME = 15;
constexpr int16_t AY_PEAK_AMPLITUDE_MAX_VOL = 18776;

class MockingboardHarness {
 public:
  MockingboardHarness() {
    s_active_harness = this;
    g_full_speed = false;
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.GetConfig = Mock_GetConfig;
    host_.AudioPushSamples = Mock_AudioPushSamples;
    host_.GetCycles = Mock_GetCycles;
  }

  ~MockingboardHarness() {
    g_full_speed = false;
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

  auto command(uint32_t cmd_id, const void* data, size_t size,
               void* inst = nullptr) -> PeripheralStatus_t {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (target == nullptr) {
      return peripheral_error;
    }
    return descriptor()->command(target, cmd_id, data, size);
  }

  auto query(uint32_t query_id, void* out, size_t* out_size,
             void* inst = nullptr) -> PeripheralStatus_t {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (target == nullptr) {
      return peripheral_error;
    }
    return descriptor()->query(target, query_id, out, out_size);
  }

  auto read_c0(uint16_t addr, void* inst = nullptr) -> uint8_t {
    void* target = (inst != nullptr) ? inst : primary_instance_;
    if (read_c0_handler_ != nullptr && target != nullptr) {
      return read_c0_handler_(target, 0, addr, 0, 0, 0);
    }
    return 0xFF;
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
    return 0xFF;
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

  auto last_registered_slot() const -> int { return last_registered_slot_; }

  auto cycles() const -> uint64_t { return cycles_; }
  auto set_cycles(uint64_t c) -> void { cycles_ = c; }
  auto advance_cycles(uint64_t delta) -> void { cycles_ += delta; }

  auto irq_asserted() const -> bool { return irq_asserted_; }
  auto irq_slot() const -> int { return irq_slot_; }
  auto irq_asserted_for_slot(int slot) const -> bool {
    return (slot >= 0 && slot < 8) ? slot_irq_asserted_[slot] : false;
  }

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
      if (slot >= 0 && slot < 8) {
        s_active_harness->slot_irq_asserted_[slot] = assert_irq;
      }
    }
  }

  static auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                              PeripheralIOHandler write_c0,
                              PeripheralIOHandler read_cx,
                              PeripheralIOHandler write_cx) -> void {
    if (s_active_harness != nullptr) {
      s_active_harness->last_registered_slot_ = slot;
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
  bool slot_irq_asserted_[8]{false, false, false, false,
                             false, false, false, false};
  std::string config_type_{};

  int last_registered_slot_{-1};
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

auto write_mockingboard_ay(MockingboardHarness& harness, uint8_t reg,
                           uint8_t val, void* inst = nullptr) -> void {
  // Latch register
  harness.write_cx(0xC001, reg, inst);
  harness.write_cx(0xC000, 0x07, inst);  // BDIR=1, BC1=1 + RESET_N
  harness.write_cx(0xC000, 0x04, inst);  // Inactive + RESET_N
  // Write data
  harness.write_cx(0xC001, val, inst);
  harness.write_cx(0xC000, 0x06, inst);  // BDIR=1, BC1=0 + RESET_N
  harness.write_cx(0xC000, 0x04, inst);  // Inactive + RESET_N
}

}  // namespace

TEST_CASE("Mockingboard Peripheral: MB-01 Descriptor Identity & Registration") {
  const Peripheral_t* desc = mockingboard_get_descriptor();
  REQUIRE(desc != nullptr);
  CHECK(desc->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(desc->id) == "linapple.mockingboard");
  CHECK(std::string(desc->name) == "Mockingboard");
  CHECK(desc->compatible_slots == PERIPHERAL_MASK_EXPANSION);
  CHECK(desc->default_slot == DEFAULT_MOCKINGBOARD_SLOT);
  CHECK(desc->init != nullptr);
  CHECK(desc->reset != nullptr);
  CHECK(desc->shutdown != nullptr);
  CHECK(desc->think != nullptr);
  CHECK(desc->save_state != nullptr);
  CHECK(desc->load_state != nullptr);
  CHECK(desc->command != nullptr);
  CHECK(desc->query != nullptr);
}

TEST_CASE("Mockingboard Peripheral: MB-02 Lifecycle & Defensive Null Guards") {
  const Peripheral_t* desc = mockingboard_get_descriptor();
  REQUIRE(desc != nullptr);

  // Null host rejects cleanly
  CHECK(desc->init(DEFAULT_MOCKINGBOARD_SLOT, nullptr) == nullptr);

  // Host missing RegisterIO rejects cleanly
  HostInterface_t bad_host{};
  CHECK(desc->init(DEFAULT_MOCKINGBOARD_SLOT, &bad_host) == nullptr);

  // Null instance safety across entry points
  desc->reset(nullptr);
  desc->shutdown(nullptr);
  desc->think(nullptr, 100);

  size_t state_size = 0;
  uint8_t dummy_byte = 0;
  CHECK(desc->save_state(nullptr, &dummy_byte, &state_size) ==
        peripheral_error);
  CHECK(desc->load_state(nullptr, &dummy_byte, 10) == peripheral_error);
  CHECK(desc->command(nullptr, mockingboard_cmd_reset_audio, nullptr, 0) ==
        peripheral_error);
  CHECK(desc->query(nullptr, mockingboard_query_status, &dummy_byte,
                    &state_size) == peripheral_error);
}

TEST_CASE("Mockingboard Peripheral: MB-03 Bus MMIO Registration") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  CHECK(harness.last_registered_slot() == 4);
  CHECK(harness.has_read_cx());
  CHECK(harness.has_write_cx());
}

TEST_CASE("Mockingboard Peripheral: MB-04 Dual 6522 VIA Register Read/Write") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  // VIA A at offset 0x00..0x0F (DDRB = 0x02, DDRA = 0x03)
  harness.write_cx(0xC002, 0x5A);
  harness.write_cx(0xC003, 0xA5);
  CHECK(harness.read_cx(0xC002) == 0x5A);
  CHECK(harness.read_cx(0xC003) == 0xA5);

  // VIA B at offset 0x80..0x8F (DDRB = 0x82, DDRA = 0x83)
  harness.write_cx(0xC082, 0x33);
  harness.write_cx(0xC083, 0xCC);
  CHECK(harness.read_cx(0xC082) == 0x33);
  CHECK(harness.read_cx(0xC083) == 0xCC);

  // Verify VIA A remains unchanged
  CHECK(harness.read_cx(0xC002) == 0x5A);
  CHECK(harness.read_cx(0xC003) == 0xA5);
}

TEST_CASE("Mockingboard Peripheral: MB-05 Floating Bus Fallthrough") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  // Unmapped register offset $10 in slot 4 ($C410)
  uint8_t unmapped_val = harness.read_cx(0xC010);
  CHECK(unmapped_val == 0xFF);
}

TEST_CASE(
    "Mockingboard Peripheral: MB-06 6522 Timer 1 Underflow & IRQ Generation") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  // Enable T1 interrupt in IER ($C40E)
  harness.write_cx(0xC00E, 0xC0);

  // Set T1 period to 1000 cycles
  harness.write_cx(0xC004, 0xE8);
  harness.write_cx(0xC005, 0x03);

  CHECK_FALSE(harness.irq_asserted());

  // Advance partially
  harness.advance_cycles(500);
  harness.think(0);
  CHECK_FALSE(harness.irq_asserted());

  // Advance past period (total 1100 > 1000)
  harness.advance_cycles(600);
  harness.think(0);
  CHECK(harness.irq_asserted());
  CHECK(harness.irq_slot() == 4);
}

TEST_CASE("Mockingboard Peripheral: MB-07 6522 Timer 1 IRQ Acknowledge") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  harness.write_cx(0xC00E, 0xC0);
  harness.write_cx(0xC004, 0xE8);
  harness.write_cx(0xC005, 0x03);

  harness.advance_cycles(1100);
  harness.think(0);
  REQUIRE(harness.irq_asserted());

  // Acknowledge IRQ by reading T1L-L ($C404)
  harness.read_cx(0xC004);
  harness.think(0);
  CHECK_FALSE(harness.irq_asserted());
}

TEST_CASE(
    "Mockingboard Peripheral: MB-08 Dual AY-3-8910 Bus Interface Protocol") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  harness.write_cx(0xC002, 0xFF);  // DDRB output
  harness.write_cx(0xC003, 0xFF);  // DDRA output
  harness.write_cx(0xC000, 0x04);  // RESET_N high

  // Latch register 7 and write 0x3F
  write_mockingboard_ay(harness, 0x07, 0x3F);

  // Save state and verify register 7 is stored as 0x3F
  size_t state_size = 0;
  REQUIRE(harness.save_state(nullptr, &state_size) == peripheral_ok);
  std::vector<uint8_t> buffer(state_size);
  REQUIRE(harness.save_state(buffer.data(), &state_size) == peripheral_ok);

  const auto* ss =
      reinterpret_cast<const MockingboardSaveState_t*>(buffer.data());
  CHECK(ss->chips[0].ay_regs[7] == 0x3F);
}

TEST_CASE(
    "Mockingboard Peripheral: MB-09 Audio Sample Synthesis & Volume Clamping") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  harness.write_cx(0xC002, 0xFF);
  harness.write_cx(0xC003, 0xFF);
  harness.write_cx(0xC000, 0x04);

  // Tone period 20 on Channel A, Volume 15
  write_mockingboard_ay(harness, 0x00, 20);
  write_mockingboard_ay(harness, 0x01, 0x00);
  write_mockingboard_ay(harness, 0x08, AY_MAX_VOLUME);
  write_mockingboard_ay(harness, 0x07, 0x3E);  // Enable Tone A

  // Setup periodic Timer 1
  harness.write_cx(0xC00E, 0xC0);
  harness.write_cx(0xC004, 0xE8);
  harness.write_cx(0xC005, 0x03);

  harness.clear_audio();
  harness.advance_cycles(1001);
  harness.think(0);

  CHECK(harness.irq_asserted());
  CHECK(harness.audio_push_call_count() == 1);
  const auto& samples = harness.audio_samples();
  REQUIRE_FALSE(samples.empty());

  int16_t peak_left = 0;
  int16_t min_left = 0;
  for (int16_t sample : samples) {
    if (sample > peak_left) peak_left = sample;
    if (sample < min_left) min_left = sample;
  }

  CHECK(peak_left == AY_PEAK_AMPLITUDE_MAX_VOL);
  CHECK(min_left == 0);
}

TEST_CASE("Mockingboard Peripheral: MB-10 60 Hz Fallback Audio Spindown") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  harness.clear_audio();
  // Step more than 1/60th second (> 17008 cycles at 1.020484 MHz) with inactive
  // timers
  harness.advance_cycles(20000);
  harness.think(0);

  CHECK(harness.audio_push_call_count() >= 1);
}

TEST_CASE(
    "Mockingboard Peripheral: MB-11 Warp Speed (g_full_speed) Suppression") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  harness.write_cx(0xC002, 0xFF);
  harness.write_cx(0xC003, 0xFF);
  harness.write_cx(0xC000, 0x04);
  write_mockingboard_ay(harness, 0x00, 20);
  write_mockingboard_ay(harness, 0x08, AY_MAX_VOLUME);
  write_mockingboard_ay(harness, 0x07, 0x3E);

  harness.write_cx(0xC00E, 0xC0);
  harness.write_cx(0xC004, 0xE8);
  harness.write_cx(0xC005, 0x03);

  // In warp speed, audio push is suppressed
  g_full_speed = true;
  harness.clear_audio();
  harness.advance_cycles(1001);
  harness.think(0);

  CHECK(harness.audio_push_call_count() == 0);

  // When normal speed resumes, audio push operates
  g_full_speed = false;
  harness.read_cx(0xC004);  // Ack IRQ
  harness.write_cx(0xC005, 0x03);
  harness.advance_cycles(1001);
  harness.think(0);

  CHECK(harness.audio_push_call_count() >= 1);
}

TEST_CASE("Mockingboard Peripheral: MB-12 Phasor Emulation Mode Switching") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  const uint8_t card_type = mockingboard_type_phasor;
  CHECK(harness.command(mockingboard_cmd_set_type, &card_type,
                        sizeof(card_type)) == peripheral_ok);

  MockingboardStatus_t status{};
  size_t status_size = sizeof(status);
  REQUIRE(harness.query(mockingboard_query_status, &status, &status_size) ==
          peripheral_ok);
  CHECK(status.card_type == mockingboard_type_phasor);
  CHECK(status.phasor_native == 0);

  // Access C0nX range ($C0C1) to trigger native mode
  harness.read_c0(0xC0C1);

  status_size = sizeof(status);
  REQUIRE(harness.query(mockingboard_query_status, &status, &status_size) ==
          peripheral_ok);
  CHECK(status.phasor_native == 1);

  // Chip A via bit 2 ($C0C6), register 6
  harness.write_c0(0xC0C6, 0xAA);
  CHECK(harness.read_c0(0xC0C6) == 0xAA);

  // Chip B via bit 3 ($C0CA), register 0xA
  harness.write_c0(0xC0CA, 0xBB);
  CHECK(harness.read_c0(0xC0CA) == 0xBB);
}

TEST_CASE("Mockingboard Peripheral: MB-13 Multi-Slot Concurrency") {
  MockingboardHarness harness;
  void* instance1 = harness.create_card(4);
  void* instance2 = harness.create_card(5);
  REQUIRE(instance1 != nullptr);
  REQUIRE(instance2 != nullptr);
  CHECK(instance1 != instance2);

  // Write different values to VIA A DDRB on each card
  harness.write_cx(0xC002, 0x11, instance1);
  harness.write_cx(0xC002, 0x22, instance2);

  CHECK(harness.read_cx(0xC002, instance1) == 0x11);
  CHECK(harness.read_cx(0xC002, instance2) == 0x22);
}

TEST_CASE("Mockingboard Peripheral: MB-14 Command ABI Parameter Validation") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  // Valid set type command
  const uint8_t valid_type = mockingboard_type_phasor;
  CHECK(harness.command(mockingboard_cmd_set_type, &valid_type,
                        sizeof(valid_type)) == peripheral_ok);

  // Undersized payload rejected
  CHECK(harness.command(mockingboard_cmd_set_type, &valid_type, 0) ==
        peripheral_error);

  // Null payload rejected
  CHECK(harness.command(mockingboard_cmd_set_type, nullptr,
                        sizeof(valid_type)) == peripheral_error);

  // Reset audio command succeeds
  CHECK(harness.command(mockingboard_cmd_reset_audio, nullptr, 0) ==
        peripheral_ok);

  // Unknown command rejected with peripheral_incompatible
  CHECK(harness.command(0x9999, nullptr, 0) == peripheral_incompatible);
}

TEST_CASE("Mockingboard Peripheral: MB-15 Query ABI Protocol & Sizing Probes") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  // Pass 1: Sizing probe with null out buffer
  size_t query_size = 0;
  CHECK(harness.query(mockingboard_query_status, nullptr, &query_size) ==
        peripheral_ok);
  CHECK(query_size == sizeof(MockingboardStatus_t));

  // Undersized buffer returns peripheral_error and sets required size
  MockingboardStatus_t status{};
  query_size = sizeof(MockingboardStatus_t) - 1;
  CHECK(harness.query(mockingboard_query_status, &status, &query_size) ==
        peripheral_error);
  CHECK(query_size == sizeof(MockingboardStatus_t));

  // Pass 2: Query execution with adequate buffer
  CHECK(harness.query(mockingboard_query_status, &status, &query_size) ==
        peripheral_ok);
  CHECK(query_size == sizeof(MockingboardStatus_t));
  CHECK(status.card_type == mockingboard_type_mockingboard);
  CHECK(status.phasor_native == 0);

  // Unknown query rejected with peripheral_incompatible
  CHECK(harness.query(0x9999, &status, &query_size) == peripheral_incompatible);
}

TEST_CASE(
    "Mockingboard Peripheral: MB-16 Deterministic 100% Save State Round Trip") {
  MockingboardHarness harness;
  void* instance1 = harness.create_card(4);
  void* instance2 = harness.create_card(5);
  REQUIRE(instance1 != nullptr);
  REQUIRE(instance2 != nullptr);

  // Mutate Card 1 state
  harness.write_cx(0xC002, 0x55, instance1);  // VIA A DDRB
  harness.write_cx(0xC083, 0xAA, instance1);  // VIA B DDRA
  const uint8_t card_type = mockingboard_type_phasor;
  REQUIRE(harness.command(mockingboard_cmd_set_type, &card_type,
                          sizeof(card_type), instance1) == peripheral_ok);

  // Sizing probe
  size_t state_size = 0;
  CHECK(harness.save_state(nullptr, &state_size, instance1) == peripheral_ok);
  REQUIRE(state_size == sizeof(MockingboardSaveState_t));
  CHECK(state_size == 232);

  // Save state
  std::vector<uint8_t> buffer(state_size);
  REQUIRE(harness.save_state(buffer.data(), &state_size, instance1) ==
          peripheral_ok);

  const auto* ss =
      reinterpret_cast<const MockingboardSaveState_t*>(buffer.data());
  CHECK(ss->version == MOCKINGBOARD_STATE_VERSION);
  CHECK(ss->struct_size == sizeof(MockingboardSaveState_t));
  CHECK(ss->chips[0].ddrb == 0x55);
  CHECK(ss->chips[1].ddra == 0xAA);
  CHECK(ss->card_type == mockingboard_type_phasor);

  // Restore into Card 2
  REQUIRE(harness.load_state(buffer.data(), state_size, instance2) ==
          peripheral_ok);

  CHECK(harness.read_cx(0xC002, instance2) == 0x55);
  CHECK(harness.read_cx(0xC083, instance2) == 0xAA);

  MockingboardStatus_t status{};
  size_t status_size = sizeof(status);
  REQUIRE(harness.query(mockingboard_query_status, &status, &status_size,
                        instance2) == peripheral_ok);
  CHECK(status.card_type == mockingboard_type_phasor);
}

TEST_CASE("Mockingboard Peripheral: MB-17 Corrupt Save State Rejection") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  size_t state_size = 0;
  REQUIRE(harness.save_state(nullptr, &state_size) == peripheral_ok);
  std::vector<uint8_t> buffer(state_size);
  REQUIRE(harness.save_state(buffer.data(), &state_size) == peripheral_ok);

  // Null buffer rejected
  CHECK(harness.load_state(nullptr, state_size) == peripheral_error);

  // Undersized buffer rejected
  CHECK(harness.load_state(buffer.data(), state_size - 1) == peripheral_error);

  // Oversized buffer rejected
  std::vector<uint8_t> oversized(state_size + 1);
  std::memcpy(oversized.data(), buffer.data(), state_size);
  CHECK(harness.load_state(oversized.data(), oversized.size()) ==
        peripheral_error);

  // Corrupt version rejected
  auto* ss = reinterpret_cast<MockingboardSaveState_t*>(buffer.data());
  uint32_t orig_ver = ss->version;
  ss->version = 999;
  CHECK(harness.load_state(buffer.data(), state_size) == peripheral_error);
  ss->version = orig_ver;

  // Corrupt struct_size rejected
  uint32_t orig_sz = ss->struct_size;
  ss->struct_size = 123;
  CHECK(harness.load_state(buffer.data(), state_size) == peripheral_error);
  ss->struct_size = orig_sz;

  // Clean state loads successfully
  CHECK(harness.load_state(buffer.data(), state_size) == peripheral_ok);
}
