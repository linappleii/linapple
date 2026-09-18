// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) Justification: Hardware register addresses, bus bit patterns and cycle-count goldens
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "apple2/peripherals/mockingboard/MockingboardCommands.h"
#include "doctest.h"

namespace {

constexpr int DEFAULT_MOCKINGBOARD_SLOT = 4;
constexpr int AY_MAX_VOLUME = 15;
constexpr size_t MB_VOICES = 6;

// $C400 selects VIA A and $C480 selects VIA B in slot 4.
constexpr uint16_t VIA_A = 0xC400;
constexpr uint16_t VIA_B = 0xC480;

constexpr uint16_t REG_ORB = 0x0;
constexpr uint16_t REG_ORA = 0x1;
constexpr uint16_t REG_DDRB = 0x2;
constexpr uint16_t REG_DDRA = 0x3;
constexpr uint16_t REG_T1C_L = 0x4;
constexpr uint16_t REG_T1C_H = 0x5;
constexpr uint16_t REG_T2C_L = 0x8;
constexpr uint16_t REG_T2C_H = 0x9;
constexpr uint16_t REG_ACR = 0xB;
constexpr uint16_t REG_IFR = 0xD;
constexpr uint16_t REG_IER = 0xE;

constexpr uint8_t IER_SET_T1 = 0xC0;
constexpr uint8_t IER_SET_T2 = 0xA0;
constexpr uint8_t IFR_T1 = 0x40;
constexpr uint8_t IFR_T2 = 0x20;
constexpr uint8_t ACR_FREE_RUN = 0x40;

// ORB patterns with /RESET held high.
constexpr uint8_t ORB_INACTIVE = 0x04;
constexpr uint8_t ORB_WRITE = 0x06;
constexpr uint8_t ORB_LATCH = 0x07;
constexpr uint8_t ORB_READ = 0x05;
constexpr uint8_t ORB_RESET = 0x00;

// One AY sample per eight 6502 cycles.
constexpr uint32_t CYCLES_PER_TICK = 8;
constexpr uint32_t DC_TAU_TICKS = 1000;

class MockingboardHarness {
 public:
  MockingboardHarness() {
    s_active_harness = this;
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.AudioPushChannels = Mock_AudioPushChannels;
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
  static auto descriptor() -> Peripheral_t* {
    return mockingboard_get_descriptor();
  }

  auto create_card(int slot = DEFAULT_MOCKINGBOARD_SLOT) -> void* {
    void* instance = descriptor()->init(slot, &host_);
    if (instance != nullptr) {
      instances_.push_back(instance);
      if (primary_instance_ == nullptr) {
        primary_instance_ = instance;
      }
      descriptor()->reset(instance);
      clear_irq_log();
    }
    return instance;
  }

  auto primary_instance() const -> void* { return primary_instance_; }

  auto reset(void* inst = nullptr) -> void {
    void* target = target_of(inst);
    if (target != nullptr) {
      descriptor()->reset(target);
    }
  }

  auto think(uint32_t cycles, void* inst = nullptr) -> void {
    void* target = target_of(inst);
    if (target != nullptr) {
      descriptor()->think(target, cycles);
    }
  }

  auto save_state(void* buffer, size_t* size, void* inst = nullptr)
      -> PeripheralStatus_t {
    void* target = target_of(inst);
    return (target == nullptr) ? peripheral_error
                               : descriptor()->save_state(target, buffer, size);
  }

  auto load_state(const void* buffer, size_t size, void* inst = nullptr)
      -> PeripheralStatus_t {
    void* target = target_of(inst);
    return (target == nullptr) ? peripheral_error
                               : descriptor()->load_state(target, buffer, size);
  }

  auto query(uint32_t query_id, void* out, size_t* out_size,
             void* inst = nullptr) -> PeripheralStatus_t {
    void* target = target_of(inst);
    return (target == nullptr)
               ? peripheral_error
               : descriptor()->query(target, query_id, out, out_size);
  }

  auto read_cx(uint16_t addr, uint32_t executed_cycles = 0,
               void* inst = nullptr) -> uint8_t {
    void* target = target_of(inst);
    if (read_cx_handler_ == nullptr || target == nullptr) {
      return 0xFF;
    }
    return read_cx_handler_(target, 0, addr, 0, 0, executed_cycles);
  }

  auto write_cx(uint16_t addr, uint8_t val, uint32_t executed_cycles = 0,
                void* inst = nullptr) -> uint8_t {
    void* target = target_of(inst);
    if (write_cx_handler_ == nullptr || target == nullptr) {
      return 0;
    }
    return write_cx_handler_(target, 0, addr, 1, val, executed_cycles);
  }

  auto has_read_c0() const -> bool { return read_c0_handler_ != nullptr; }
  auto has_write_c0() const -> bool { return write_c0_handler_ != nullptr; }
  auto has_read_cx() const -> bool { return read_cx_handler_ != nullptr; }
  auto has_write_cx() const -> bool { return write_cx_handler_ != nullptr; }
  auto register_io_calls() const -> int { return register_io_calls_; }
  auto last_registered_slot() const -> int { return last_registered_slot_; }

  auto irq_asserted() const -> bool { return irq_asserted_; }
  auto irq_slot() const -> int { return irq_slot_; }
  auto irq_log() const -> const std::vector<bool>& { return irq_log_; }
  auto clear_irq_log() -> void { irq_log_.clear(); }

  auto channel(size_t index) const -> const std::vector<float>& {
    static const std::vector<float> empty;
    return (index < channels_.size()) ? channels_[index] : empty;
  }
  auto captured_channel_count() const -> size_t { return channel_count_; }
  auto push_count() const -> uint32_t { return push_count_; }
  auto last_push_samples() const -> size_t { return last_push_samples_; }
  auto total_pushed_samples() const -> size_t { return total_pushed_samples_; }
  auto clear_audio() -> void {
    channels_.clear();
    channel_count_ = 0;
    push_count_ = 0;
    last_push_samples_ = 0;
    total_pushed_samples_ = 0;
  }

  auto disable_audio_push() -> void { host_.AudioPushChannels = nullptr; }
  auto disable_assert_irq() -> void { host_.AssertIrq = nullptr; }

 private:
  auto target_of(void* inst) const -> void* {
    return (inst != nullptr) ? inst : primary_instance_;
  }

  static auto Mock_AssertIrq(int slot, bool assert_irq) -> void {
    if (s_active_harness == nullptr) {
      return;
    }
    s_active_harness->irq_asserted_ = assert_irq;
    s_active_harness->irq_slot_ = slot;
    s_active_harness->irq_log_.push_back(assert_irq);
  }

  static auto Mock_RegisterIO(int slot, PeripheralIOHandler read_c0,
                              PeripheralIOHandler write_c0,
                              PeripheralIOHandler read_cx,
                              PeripheralIOHandler write_cx) -> void {
    if (s_active_harness == nullptr) {
      return;
    }
    s_active_harness->register_io_calls_++;
    s_active_harness->last_registered_slot_ = slot;
    s_active_harness->read_c0_handler_ = read_c0;
    s_active_harness->write_c0_handler_ = write_c0;
    s_active_harness->read_cx_handler_ = read_cx;
    s_active_harness->write_cx_handler_ = write_cx;
  }

  static auto Mock_AudioPushChannels(void* instance,
                                     const float* const* channels,
                                     size_t num_channels, size_t num_samples)
      -> void {
    (void)instance;
    if (s_active_harness == nullptr || channels == nullptr ||
        num_channels == 0 || num_samples == 0) {
      return;
    }
    s_active_harness->channel_count_ = num_channels;
    if (s_active_harness->channels_.size() < num_channels) {
      s_active_harness->channels_.resize(num_channels);
    }
    for (size_t c = 0; c < num_channels; ++c) {
      if (channels[c] == nullptr) {
        continue;
      }
      for (size_t i = 0; i < num_samples; ++i) {
        s_active_harness->channels_[c].push_back(channels[c][i]);
      }
    }
    s_active_harness->last_push_samples_ = num_samples;
    s_active_harness->total_pushed_samples_ += num_samples;
    s_active_harness->push_count_++;
  }

  static MockingboardHarness* s_active_harness;

  HostInterface_t host_{};
  bool irq_asserted_{false};
  int irq_slot_{-1};
  std::vector<bool> irq_log_{};

  int register_io_calls_{0};
  int last_registered_slot_{-1};
  PeripheralIOHandler read_c0_handler_{nullptr};
  PeripheralIOHandler write_c0_handler_{nullptr};
  PeripheralIOHandler read_cx_handler_{nullptr};
  PeripheralIOHandler write_cx_handler_{nullptr};

  std::vector<std::vector<float>> channels_{};
  size_t channel_count_{0};
  size_t last_push_samples_{0};
  size_t total_pushed_samples_{0};
  uint32_t push_count_{0};

  std::vector<void*> instances_{};
  void* primary_instance_{nullptr};
};

MockingboardHarness* MockingboardHarness::s_active_harness = nullptr;

// Drives the AY control bus the way software does: park the register number
// on port A, strobe latch, park the value, strobe write.
auto write_ay(MockingboardHarness& harness, uint16_t via, uint8_t reg,
              uint8_t val, uint32_t executed_cycles = 0, void* inst = nullptr)
    -> void {
  harness.write_cx(via + REG_ORA, reg, executed_cycles, inst);
  harness.write_cx(via + REG_ORB, ORB_LATCH, executed_cycles, inst);
  harness.write_cx(via + REG_ORB, ORB_INACTIVE, executed_cycles, inst);
  harness.write_cx(via + REG_ORA, val, executed_cycles, inst);
  harness.write_cx(via + REG_ORB, ORB_WRITE, executed_cycles, inst);
  harness.write_cx(via + REG_ORB, ORB_INACTIVE, executed_cycles, inst);
}

auto open_ay_ports(MockingboardHarness& harness, uint16_t via,
                   void* inst = nullptr) -> void {
  harness.write_cx(via + REG_DDRB, 0xFF, 0, inst);
  harness.write_cx(via + REG_DDRA, 0xFF, 0, inst);
  harness.write_cx(via + REG_ORB, ORB_INACTIVE, 0, inst);
}

// Voice A at a fixed volume with tone and noise off: the gate reads high, so
// the chip emits a constant level and the card's coupling turns it into a
// step. The simplest signal a golden can name a value for.
auto set_voice_a_dc(MockingboardHarness& harness, uint8_t volume,
                    uint32_t executed_cycles = 0) -> void {
  write_ay(harness, VIA_A, 0x07, 0x3F, executed_cycles);
  write_ay(harness, VIA_A, 0x08, volume, executed_cycles);
}

auto arm_timer1(MockingboardHarness& harness, uint16_t via, uint16_t latch,
                uint8_t acr = 0x00, void* inst = nullptr) -> void {
  harness.write_cx(via + REG_ACR, acr, 0, inst);
  harness.write_cx(via + REG_IER, IER_SET_T1, 0, inst);
  harness.write_cx(via + REG_T1C_L, static_cast<uint8_t>(latch & 0xFF), 0,
                   inst);
  harness.write_cx(via + REG_T1C_H, static_cast<uint8_t>(latch >> 8), 0, inst);
}

}  // namespace

TEST_CASE("Mockingboard Peripheral: MB-01 Descriptor Identity & Registration") {
  const Peripheral_t* desc = mockingboard_get_descriptor();
  REQUIRE(desc != nullptr);
  CHECK(desc->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(desc->id) == "linapple.mockingboard");
  CHECK(std::string(desc->name) == "Mockingboard");
  CHECK(std::string(desc->description) ==
        "Sweet Micro Systems Mockingboard (2x 6522, 2x AY-3-8910)");
  CHECK(desc->compatible_slots == PERIPHERAL_MASK_EXPANSION);
  CHECK(desc->default_slot == DEFAULT_MOCKINGBOARD_SLOT);
  CHECK(desc->init != nullptr);
  CHECK(desc->reset != nullptr);
  CHECK(desc->shutdown != nullptr);
  CHECK(desc->think != nullptr);
  CHECK(desc->save_state != nullptr);
  CHECK(desc->load_state != nullptr);
  CHECK(desc->query != nullptr);
  // The card has no physical control a frontend can operate.
  CHECK(desc->command == nullptr);
  CHECK(desc->on_vblank == nullptr);
}

TEST_CASE("Mockingboard Peripheral: MB-02 Lifecycle & Defensive Null Guards") {
  const Peripheral_t* desc = mockingboard_get_descriptor();
  REQUIRE(desc != nullptr);

  CHECK(desc->init(DEFAULT_MOCKINGBOARD_SLOT, nullptr) == nullptr);

  HostInterface_t bad_host{};
  CHECK(desc->init(DEFAULT_MOCKINGBOARD_SLOT, &bad_host) == nullptr);

  desc->reset(nullptr);
  desc->shutdown(nullptr);
  desc->think(nullptr, 100);

  size_t state_size = 0;
  uint8_t dummy_byte = 0;
  CHECK(desc->save_state(nullptr, &dummy_byte, &state_size) ==
        peripheral_error);
  CHECK(desc->load_state(nullptr, &dummy_byte, 10) == peripheral_error);
  CHECK(desc->query(nullptr, PERIPHERAL_QUERY_AUDIO_INFO, &dummy_byte,
                    &state_size) == peripheral_error);
}

TEST_CASE("Mockingboard Peripheral: MB-03 Bus MMIO Registration") {
  MockingboardHarness harness;
  void* instance = harness.create_card(4);
  REQUIRE(instance != nullptr);

  CHECK(harness.register_io_calls() == 1);
  CHECK(harness.last_registered_slot() == 4);
  // The card decodes /IO SELECT only: there is no $C0n0 device-select range
  // on a Mockingboard.
  CHECK_FALSE(harness.has_read_c0());
  CHECK_FALSE(harness.has_write_c0());
  CHECK(harness.has_read_cx());
  CHECK(harness.has_write_cx());
}

TEST_CASE("Mockingboard Peripheral: MB-04 Dual 6522 VIA Register Read/Write") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  harness.write_cx(VIA_A + REG_DDRB, 0x5A);
  harness.write_cx(VIA_A + REG_DDRA, 0xA5);
  CHECK(harness.read_cx(VIA_A + REG_DDRB) == 0x5A);
  CHECK(harness.read_cx(VIA_A + REG_DDRA) == 0xA5);

  harness.write_cx(VIA_B + REG_DDRB, 0x33);
  harness.write_cx(VIA_B + REG_DDRA, 0xCC);
  CHECK(harness.read_cx(VIA_B + REG_DDRB) == 0x33);
  CHECK(harness.read_cx(VIA_B + REG_DDRA) == 0xCC);

  CHECK(harness.read_cx(VIA_A + REG_DDRB) == 0x5A);
  CHECK(harness.read_cx(VIA_A + REG_DDRA) == 0xA5);
}

TEST_CASE("Mockingboard Peripheral: MB-06 6522 Timer 1 Underflow & IRQ") {
  constexpr uint16_t latch = 1000;
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  arm_timer1(harness, VIA_A, latch);
  CHECK_FALSE(harness.irq_asserted());

  harness.think(latch + 1);
  CHECK_FALSE(harness.irq_asserted());

  harness.think(1);
  CHECK(harness.irq_asserted());
  CHECK(harness.irq_slot() == 4);
}

TEST_CASE("Mockingboard Peripheral: MB-07 6522 Timer 1 IRQ Acknowledge") {
  constexpr uint16_t latch = 1000;
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  arm_timer1(harness, VIA_A, latch);
  harness.think(latch + 2);
  REQUIRE(harness.irq_asserted());

  harness.read_cx(VIA_A + REG_T1C_L);
  CHECK_FALSE(harness.irq_asserted());

  // One call to assert and one to deassert: the line is reported when it
  // changes and at no other time.
  CHECK(harness.irq_log().size() == 2);
  CHECK(harness.irq_log()[0]);
  CHECK_FALSE(harness.irq_log()[1]);
}

TEST_CASE("Mockingboard Peripheral: MB-08 AY-3-8910 Bus Interface Protocol") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  write_ay(harness, VIA_A, 0x07, 0x3F);

  size_t state_size = 0;
  REQUIRE(harness.save_state(nullptr, &state_size) == peripheral_ok);
  std::vector<uint8_t> buffer(state_size);
  REQUIRE(harness.save_state(buffer.data(), &state_size) == peripheral_ok);

  const auto* ss =
      reinterpret_cast<const MockingboardSaveState_t*>(buffer.data());
  CHECK(ss->chips[0].ay_regs[7] == 0x3F);
  CHECK(ss->chips[1].ay_regs[7] == 0x00);
}

TEST_CASE("Mockingboard Peripheral: MB-13 Multi-Slot Concurrency") {
  MockingboardHarness harness;
  void* card4 = harness.create_card(4);
  void* card5 = harness.create_card(5);
  REQUIRE(card4 != nullptr);
  REQUIRE(card5 != nullptr);
  CHECK(card4 != card5);

  harness.write_cx(VIA_A + REG_DDRB, 0x11, 0, card4);
  harness.write_cx(VIA_A + REG_DDRB, 0x22, 0, card5);
  CHECK(harness.read_cx(VIA_A + REG_DDRB, 0, card4) == 0x11);
  CHECK(harness.read_cx(VIA_A + REG_DDRB, 0, card5) == 0x22);

  // A timer on one card interrupts on its own slot and not on the other's.
  arm_timer1(harness, VIA_A, 500, 0x00, card5);
  harness.think(502, card5);
  CHECK(harness.irq_asserted());
  CHECK(harness.irq_slot() == 5);
}

TEST_CASE("Mockingboard Peripheral: MB-15 Query ABI Protocol & Sizing Probes") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  size_t query_size = 0;
  CHECK(harness.query(PERIPHERAL_QUERY_AUDIO_INFO, nullptr, &query_size) ==
        peripheral_ok);
  CHECK(query_size == sizeof(PeripheralAudioInfo_t));

  PeripheralAudioInfo_t info{};
  query_size = sizeof(PeripheralAudioInfo_t) - 1;
  CHECK(harness.query(PERIPHERAL_QUERY_AUDIO_INFO, &info, &query_size) ==
        peripheral_error);
  CHECK(query_size == sizeof(PeripheralAudioInfo_t));

  CHECK(harness.query(PERIPHERAL_QUERY_AUDIO_INFO, &info, &query_size) ==
        peripheral_ok);
  CHECK(query_size == sizeof(PeripheralAudioInfo_t));

  CHECK(harness.query(0x9999, &info, &query_size) == peripheral_incompatible);
}

TEST_CASE("Mockingboard Peripheral: MB-16 Save State Round Trip") {
  MockingboardHarness harness;
  void* card4 = harness.create_card(4);
  void* card5 = harness.create_card(5);
  REQUIRE(card4 != nullptr);
  REQUIRE(card5 != nullptr);

  open_ay_ports(harness, VIA_A, card4);
  harness.write_cx(VIA_A + REG_DDRB, 0x55, 0, card4);
  harness.write_cx(VIA_B + REG_DDRA, 0xAA, 0, card4);
  write_ay(harness, VIA_A, 0x00, 0x7F, 0, card4);
  arm_timer1(harness, VIA_A, 0x1234, ACR_FREE_RUN, card4);
  harness.think(600, card4);

  size_t state_size = 0;
  CHECK(harness.save_state(nullptr, &state_size, card4) == peripheral_ok);
  REQUIRE(state_size == sizeof(MockingboardSaveState_t));
  CHECK(state_size == 232);

  std::vector<uint8_t> buffer(state_size);
  REQUIRE(harness.save_state(buffer.data(), &state_size, card4) ==
          peripheral_ok);

  const auto* ss =
      reinterpret_cast<const MockingboardSaveState_t*>(buffer.data());
  CHECK(ss->version == MOCKINGBOARD_STATE_VERSION);
  CHECK(ss->struct_size == sizeof(MockingboardSaveState_t));
  CHECK(ss->chips[0].ddrb == 0x55);
  CHECK(ss->chips[1].ddra == 0xAA);
  CHECK(ss->chips[0].ay_regs[0] == 0x7F);
  CHECK(ss->chips[0].t1_latch == 0x1234);
  CHECK(ss->psg_remainder == 0);

  REQUIRE(harness.load_state(buffer.data(), state_size, card5) ==
          peripheral_ok);
  CHECK(harness.read_cx(VIA_A + REG_DDRB, 0, card5) == 0x55);
  CHECK(harness.read_cx(VIA_B + REG_DDRA, 0, card5) == 0xAA);

  // A save of the restored card is byte-identical to the one it came from.
  std::vector<uint8_t> resaved(state_size);
  size_t resaved_size = state_size;
  REQUIRE(harness.save_state(resaved.data(), &resaved_size, card5) ==
          peripheral_ok);
  CHECK(std::memcmp(buffer.data(), resaved.data(), state_size) == 0);
}

TEST_CASE("Mockingboard Peripheral: MB-16b A Pristine Card Round Trips Too") {
  MockingboardHarness harness;
  void* source = harness.create_card(4);
  void* target = harness.create_card(5);
  REQUIRE(source != nullptr);
  REQUIRE(target != nullptr);

  // Nothing is driven: the round trip has to hold for the state a card holds
  // the instant it comes out of reset, which is the state every save taken
  // before a program touches the card is made of.
  const size_t state_size = sizeof(MockingboardSaveState_t);
  std::vector<uint8_t> saved(state_size);
  std::vector<uint8_t> resaved(state_size);
  size_t saved_size = state_size;
  size_t resaved_size = state_size;

  REQUIRE(harness.save_state(saved.data(), &saved_size, source) ==
          peripheral_ok);
  REQUIRE(harness.load_state(saved.data(), state_size, target) ==
          peripheral_ok);
  REQUIRE(harness.save_state(resaved.data(), &resaved_size, target) ==
          peripheral_ok);
  CHECK(std::memcmp(saved.data(), resaved.data(), state_size) == 0);
}

TEST_CASE("Mockingboard Peripheral: MB-17 Corrupt Save State Rejection") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  size_t state_size = 0;
  REQUIRE(harness.save_state(nullptr, &state_size) == peripheral_ok);
  std::vector<uint8_t> buffer(state_size);
  REQUIRE(harness.save_state(buffer.data(), &state_size) == peripheral_ok);

  CHECK(harness.load_state(nullptr, state_size) == peripheral_error);
  CHECK(harness.load_state(buffer.data(), 0) == peripheral_error);
  CHECK(harness.load_state(buffer.data(), state_size - 1) == peripheral_error);

  std::vector<uint8_t> oversized(state_size + 1);
  std::memcpy(oversized.data(), buffer.data(), state_size);
  CHECK(harness.load_state(oversized.data(), oversized.size()) ==
        peripheral_error);

  auto* ss = reinterpret_cast<MockingboardSaveState_t*>(buffer.data());
  const uint32_t original_version = ss->version;
  ss->version = 999;
  CHECK(harness.load_state(buffer.data(), state_size) == peripheral_error);
  ss->version = original_version;

  const uint32_t original_size = ss->struct_size;
  ss->struct_size = 123;
  CHECK(harness.load_state(buffer.data(), state_size) == peripheral_error);
  ss->struct_size = original_size;

  CHECK(harness.load_state(buffer.data(), state_size) == peripheral_ok);
}

TEST_CASE("Mockingboard Peripheral: MB-18 Audio Info Query Two-Pass Contract") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  size_t size = 0;
  CHECK(harness.query(PERIPHERAL_QUERY_AUDIO_INFO, nullptr, &size) ==
        peripheral_ok);
  CHECK(size == sizeof(PeripheralAudioInfo_t));

  PeripheralAudioInfo_t info{};
  size = sizeof(PeripheralAudioInfo_t);
  CHECK(harness.query(PERIPHERAL_QUERY_AUDIO_INFO, &info, &size) ==
        peripheral_ok);
  CHECK(size == sizeof(PeripheralAudioInfo_t));
}

TEST_CASE("Mockingboard Peripheral: MB-20 Think Renders One Sample Per Eight") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  set_voice_a_dc(harness, AY_MAX_VOLUME);
  harness.clear_audio();

  harness.think(1023);
  CHECK(harness.push_count() == 1);
  CHECK(harness.last_push_samples() == 127);
  CHECK(harness.captured_channel_count() == MB_VOICES);

  // The seven cycles left over are carried, so the next single cycle
  // completes a tick.
  harness.think(1);
  CHECK(harness.push_count() == 2);
  CHECK(harness.last_push_samples() == 1);
  CHECK(harness.total_pushed_samples() == 128);
}

TEST_CASE("Mockingboard Peripheral: MB-20b Chunked Render Loses No Ticks") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  set_voice_a_dc(harness, AY_MAX_VOLUME);
  harness.clear_audio();

  // More than one scratch chunk, and not a whole number of them.
  harness.think(40000);
  CHECK(harness.total_pushed_samples() == 5000);
  CHECK(harness.push_count() == 5);
  CHECK(harness.channel(0).size() == 5000);
}

TEST_CASE("Mockingboard Peripheral: MB-21 Both VIAs Interrupt") {
  constexpr uint16_t latch = 800;
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  arm_timer1(harness, VIA_B, latch);
  harness.think(latch + 2);
  CHECK(harness.irq_asserted());
  CHECK(harness.irq_slot() == 4);

  harness.read_cx(VIA_B + REG_T1C_L);
  CHECK_FALSE(harness.irq_asserted());
}

TEST_CASE("Mockingboard Peripheral: MB-22 Timer 2 Interrupts Once") {
  constexpr uint16_t latch = 700;
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  harness.write_cx(VIA_A + REG_IER, IER_SET_T2);
  harness.write_cx(VIA_A + REG_T2C_L, static_cast<uint8_t>(latch & 0xFF));
  harness.write_cx(VIA_A + REG_T2C_H, static_cast<uint8_t>(latch >> 8));

  harness.think(latch + 1);
  CHECK_FALSE(harness.irq_asserted());
  harness.think(1);
  CHECK(harness.irq_asserted());

  harness.write_cx(VIA_A + REG_IFR, IFR_T2);
  REQUIRE_FALSE(harness.irq_asserted());
  harness.think(65536);
  CHECK_FALSE(harness.irq_asserted());
}

TEST_CASE("Mockingboard Peripheral: MB-23 Free-Run Period Is N Plus Two") {
  constexpr uint16_t latch = 0x1000;
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  arm_timer1(harness, VIA_A, latch, ACR_FREE_RUN);

  for (int period = 0; period < 10; ++period) {
    harness.think(latch + 1);
    CHECK_FALSE(harness.irq_asserted());
    harness.think(1);
    CHECK(harness.irq_asserted());
    harness.write_cx(VIA_A + REG_IFR, IFR_T1);
    REQUIRE_FALSE(harness.irq_asserted());
  }
  CHECK(harness.irq_log().size() == 20);
}

TEST_CASE("Mockingboard Peripheral: MB-24 AY Bus Protocol Round Trip") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  write_ay(harness, VIA_A, 0x02, 0xC3);
  write_ay(harness, VIA_A, 0x06, 0x1F);

  // Reading needs port A turned around so the chip can drive it.
  harness.write_cx(VIA_A + REG_ORA, 0x02);
  harness.write_cx(VIA_A + REG_ORB, ORB_LATCH);
  harness.write_cx(VIA_A + REG_ORB, ORB_INACTIVE);
  harness.write_cx(VIA_A + REG_DDRA, 0x00);
  harness.write_cx(VIA_A + REG_ORB, ORB_READ);
  CHECK(harness.read_cx(VIA_A + REG_ORA) == 0xC3);

  // /RESET low clears every register.
  harness.write_cx(VIA_A + REG_ORB, ORB_RESET);

  size_t state_size = 0;
  REQUIRE(harness.save_state(nullptr, &state_size) == peripheral_ok);
  std::vector<uint8_t> buffer(state_size);
  REQUIRE(harness.save_state(buffer.data(), &state_size) == peripheral_ok);
  const auto* ss =
      reinterpret_cast<const MockingboardSaveState_t*>(buffer.data());
  for (size_t r = 0; r < MOCKINGBOARD_AY_REGS; ++r) {
    CHECK(ss->chips[0].ay_regs[r] == 0x00);
  }
  CHECK(ss->chips[0].rng == 1);
}

TEST_CASE("Mockingboard Peripheral: MB-25 The Card's Output Is AC Coupled") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  set_voice_a_dc(harness, AY_MAX_VOLUME);
  harness.clear_audio();

  harness.think(CYCLES_PER_TICK);
  REQUIRE(harness.channel(0).size() == 1);
  CHECK(harness.channel(0)[0] == doctest::Approx(1.0F).epsilon(1e-6));

  // Five time constants in, the charge a real card would hold on its coupling
  // capacitor has gone.
  harness.think(5 * DC_TAU_TICKS * CYCLES_PER_TICK);
  const std::vector<float>& decayed = harness.channel(0);
  REQUIRE(decayed.size() > 1);
  CHECK(decayed.back() < 1e-2F);
  CHECK(decayed.back() > 0.0F);

  // Taking the level away swings the output the other way by as much as it
  // swung on the way in, which is the excursion the rails have to hold.
  harness.clear_audio();
  set_voice_a_dc(harness, 0);
  harness.think(CYCLES_PER_TICK);
  REQUIRE(harness.channel(0).size() == 1);
  CHECK(harness.channel(0)[0] < -0.99F);
  CHECK(harness.channel(0)[0] >= -1.0F);
}

TEST_CASE("Mockingboard Peripheral: MB-26 Silence Is Not Pushed") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  set_voice_a_dc(harness, AY_MAX_VOLUME);
  harness.think(CYCLES_PER_TICK * 100);
  set_voice_a_dc(harness, 0);

  // Long enough for the coupling to settle below the card's own floor.
  harness.think(15 * DC_TAU_TICKS * CYCLES_PER_TICK);

  harness.clear_audio();
  harness.think(CYCLES_PER_TICK * 4000);
  harness.think(CYCLES_PER_TICK * 4000);
  CHECK(harness.push_count() == 0);
  CHECK(harness.total_pushed_samples() == 0);
}

TEST_CASE("Mockingboard Peripheral: MB-27 Audio Info Query Zeroes Its Out") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  // The mixer compares announcements byte for byte to decide whether a
  // re-registration changed anything, so the padding has to be written too.
  std::vector<uint8_t> filled(sizeof(PeripheralAudioInfo_t), 0xFF);
  std::vector<uint8_t> clean(sizeof(PeripheralAudioInfo_t), 0x00);

  size_t size = sizeof(PeripheralAudioInfo_t);
  REQUIRE(harness.query(PERIPHERAL_QUERY_AUDIO_INFO, filled.data(), &size) ==
          peripheral_ok);
  size = sizeof(PeripheralAudioInfo_t);
  REQUIRE(harness.query(PERIPHERAL_QUERY_AUDIO_INFO, clean.data(), &size) ==
          peripheral_ok);

  CHECK(std::memcmp(filled.data(), clean.data(), filled.size()) == 0);
}

TEST_CASE("Mockingboard Peripheral: MB-29 Audio Info Contents") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  PeripheralAudioInfo_t info{};
  size_t size = sizeof(info);
  REQUIRE(harness.query(PERIPHERAL_QUERY_AUDIO_INFO, &info, &size) ==
          peripheral_ok);

  // This is the one fact that makes the mixer resolve 127,560.5 Hz.
  CHECK(info.time_base == peripheral_audio_cpu_clocked);
  CHECK(info.cycle_divisor == 8);
  CHECK(info.sample_rate == 0);
  CHECK(info.num_channels == MB_VOICES);
  CHECK(info.peak_magnitude == 1.0F);

  const char* const expected_names[MB_VOICES] = {
      "AY0 Voice A", "AY0 Voice B", "AY0 Voice C",
      "AY1 Voice A", "AY1 Voice B", "AY1 Voice C"};
  for (size_t i = 0; i < MB_VOICES; ++i) {
    CHECK(std::strcmp(info.channels[i].name, expected_names[i]) == 0);
  }
  for (size_t i = 0; i < 3; ++i) {
    CHECK(info.channels[i].default_pan_left == 1.0F);
    CHECK(info.channels[i].default_pan_right == 0.0F);
  }
  for (size_t i = 3; i < MB_VOICES; ++i) {
    CHECK(info.channels[i].default_pan_left == 0.0F);
    CHECK(info.channels[i].default_pan_right == 1.0F);
  }
}

TEST_CASE("Mockingboard Peripheral: MB-30 Timers Run Without Audio") {
  constexpr uint16_t latch = 1000;
  MockingboardHarness harness;
  harness.disable_audio_push();
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  set_voice_a_dc(harness, AY_MAX_VOLUME);
  arm_timer1(harness, VIA_A, latch);

  harness.think(latch + 2);
  CHECK(harness.irq_asserted());
  CHECK(harness.irq_slot() == 4);
  CHECK(harness.push_count() == 0);
}

TEST_CASE("Mockingboard Peripheral: MB-31 The Cx Page Aliases Onto Two VIAs") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  // A4 through A6 are unconnected, so VIA A answers every sixteen-byte step
  // below $Cn80 and VIA B every one above it.
  harness.write_cx(0xC400, 0x54);
  CHECK(harness.read_cx(0xC410) == 0x54);
  CHECK(harness.read_cx(0xC470) == 0x54);

  harness.write_cx(0xC47F, 0x3C);
  CHECK(harness.read_cx(0xC40F) == 0x3C);

  harness.write_cx(0xC480, 0x54);
  harness.write_cx(0xC4FF, 0xA9);
  CHECK(harness.read_cx(0xC48F) == 0xA9);
  CHECK(harness.read_cx(0xC480) == 0x54);

  // VIA A is untouched by any of it.
  CHECK(harness.read_cx(0xC400) == 0x54);
  CHECK(harness.read_cx(0xC40F) == 0x3C);
}

TEST_CASE("Mockingboard Peripheral: MB-32 Missing Host Calls Do Not Fault") {
  const Peripheral_t* desc = mockingboard_get_descriptor();

  SUBCASE("A null host is refused") {
    CHECK(desc->init(DEFAULT_MOCKINGBOARD_SLOT, nullptr) == nullptr);
  }

  SUBCASE("A host without RegisterIO is refused") {
    HostInterface_t host{};
    CHECK(desc->init(DEFAULT_MOCKINGBOARD_SLOT, &host) == nullptr);
  }

  SUBCASE("A host without AssertIrq still runs its timers") {
    MockingboardHarness harness;
    harness.disable_assert_irq();
    REQUIRE(harness.create_card(4) != nullptr);
    arm_timer1(harness, VIA_A, 500);
    harness.think(502);
    CHECK((harness.read_cx(VIA_A + REG_IFR) & IFR_T1) != 0);
  }

  SUBCASE("A host without AudioPushChannels still renders") {
    MockingboardHarness harness;
    harness.disable_audio_push();
    REQUIRE(harness.create_card(4) != nullptr);
    open_ay_ports(harness, VIA_A);
    set_voice_a_dc(harness, AY_MAX_VOLUME);
    harness.think(20000);
    harness.reset();
    CHECK(harness.push_count() == 0);
  }
}

TEST_CASE("Mockingboard Peripheral: MB-34 A Counter Read Sees This Cycle") {
  constexpr uint16_t latch = 0x1000;
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  arm_timer1(harness, VIA_A, latch);

  // Two reads eight cycles apart inside one slice. This difference is the
  // oracle Mad Effect 2's boot code uses to decide the card is real.
  const uint8_t first = harness.read_cx(VIA_A + REG_T1C_L, 10);
  const uint8_t second = harness.read_cx(VIA_A + REG_T1C_L, 18);
  CHECK(static_cast<uint8_t>(second - first) == 0xF8);

  // Crossing into the next slice counts every cycle exactly once: the slice
  // finishes at 100 and the next read is 5 cycles into its successor.
  harness.think(100);
  const uint8_t third = harness.read_cx(VIA_A + REG_T1C_L, 5);
  CHECK(static_cast<uint8_t>(first - third) == 100 - 10 + 5);
}

TEST_CASE("Mockingboard Peripheral: MB-35 The Counter Runs Through Reload") {
  constexpr uint16_t latch = 0x0100;
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  arm_timer1(harness, VIA_A, latch, ACR_FREE_RUN);

  harness.think(latch + 2);
  REQUIRE(harness.irq_asserted());
  CHECK(harness.read_cx(VIA_A + REG_T1C_L) == 0xFF);

  harness.think(1);
  CHECK(harness.read_cx(VIA_A + REG_T1C_L) == 0x00);

  for (uint16_t k = 1; k <= 8; ++k) {
    harness.think(1);
    CHECK(harness.read_cx(VIA_A + REG_T1C_L) ==
          static_cast<uint8_t>((latch - k) & 0xFF));
  }
}

TEST_CASE("Mockingboard Peripheral: MB-36 A Write Lands On Its Own Cycle") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  set_voice_a_dc(harness, 0);
  harness.clear_audio();

  // The first hundred ticks of the slice are silent and are not pushed, so a
  // hundred samples out of a two-hundred-tick slice is the proof that the
  // write landed at cycle 800 and not at either end of it.
  set_voice_a_dc(harness, AY_MAX_VOLUME, 800);
  harness.think(1600);

  CHECK(harness.total_pushed_samples() == 100);
  REQUIRE(harness.channel(0).size() == 100);
  CHECK(harness.channel(0)[0] == doctest::Approx(1.0F).epsilon(1e-6));
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
