// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) Justification: Hardware register addresses, bus bit patterns and cycle-count goldens
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mockingboard/MockingboardCommands.h"
#include "doctest.h"

namespace {

// The card is reached the way the emulator reaches it, through the registry,
// so one test binary covers the built-in card and the loaded plugin alike.
auto mockingboard_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.mockingboard");
}

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
constexpr uint32_t NTSC_FRAME_CYCLES = 17030;
constexpr uint32_t DC_TAU_TICKS = 1000;

class MockingboardHarness {
 public:
  MockingboardHarness() {
    s_active_harness = this;
    REQUIRE(mockingboard_descriptor() != nullptr);
    host_.AssertIrq = Mock_AssertIrq;
    host_.RegisterIO = Mock_RegisterIO;
    host_.AudioPushChannels = Mock_AudioPushChannels;
  }

  ~MockingboardHarness() {
    for (void* inst : instances_) {
      if (inst != nullptr) {
        mockingboard_descriptor()->shutdown(inst);
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
    return mockingboard_descriptor();
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

// Voice A as a square wave, so consecutive samples differ and a tick the
// render loop dropped or emitted twice shifts every sample after it.
auto set_voice_a_tone(MockingboardHarness& harness, uint16_t period) -> void {
  write_ay(harness, VIA_A, 0x00, static_cast<uint8_t>(period & 0xFF));
  write_ay(harness, VIA_A, 0x01, static_cast<uint8_t>(period >> 8));
  write_ay(harness, VIA_A, 0x07, 0x3E);
  write_ay(harness, VIA_A, 0x08, AY_MAX_VOLUME);
}

// Voice A driven by the envelope generator rather than a fixed volume, so a
// corrupted envelope step is a value the render loop actually has to survive.
auto set_voice_a_envelope(MockingboardHarness& harness) -> void {
  write_ay(harness, VIA_A, 0x07, 0x3F);
  write_ay(harness, VIA_A, 0x08, 0x10);
  write_ay(harness, VIA_A, 0x0B, 0x20);
  write_ay(harness, VIA_A, 0x0C, 0x00);
  write_ay(harness, VIA_A, 0x0D, 0x0A);
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
  const Peripheral_t* desc = mockingboard_descriptor();
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
  const Peripheral_t* desc = mockingboard_descriptor();
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

  // A blob that passes the version and size gate still carries fields that
  // index a volume table, divide a counter or select a register. Each one below
  // is a value the hardware can never hold, and the proof it was sanitized is
  // that it reads back inside its width and the next slice renders inside the
  // peak magnitude the card declares.
  void* victim = harness.create_card(5);
  REQUIRE(victim != nullptr);
  open_ay_ports(harness, VIA_A);
  set_voice_a_envelope(harness);
  harness.think(4096);

  std::vector<uint8_t> base(state_size);
  size_t base_size = state_size;
  REQUIRE(harness.save_state(base.data(), &base_size) == peripheral_ok);

  // The value has to be read back before the slice runs, because rendering
  // advances every one of these generators past whatever load left it at.
  std::vector<uint8_t> sanitized(state_size);
  const auto load_and_render =
      [&](const std::vector<uint8_t>& blob) -> const MockingboardSaveState_t* {
    harness.clear_audio();
    REQUIRE(harness.load_state(blob.data(), state_size, victim) ==
            peripheral_ok);
    size_t sanitized_size = state_size;
    REQUIRE(harness.save_state(sanitized.data(), &sanitized_size, victim) ==
            peripheral_ok);
    harness.think(1024, victim);

    bool within_peak = true;
    for (size_t v = 0; v < MB_VOICES; ++v) {
      for (const float sample : harness.channel(v)) {
        // The card's output stage is AC coupled, so a voice that is unipolar
        // inside the chip is bounded in both directions once it leaves.
        if (!(sample >= -1.0F && sample <= 1.0F)) {
          within_peak = false;
        }
      }
    }
    // Without this the range assertion above would pass on an empty push log.
    CHECK(harness.push_count() > 0);
    CHECK(within_peak);
    return reinterpret_cast<const MockingboardSaveState_t*>(sanitized.data());
  };

  SUBCASE("An envelope step past the last one is masked to its width") {
    std::vector<uint8_t> blob = base;
    reinterpret_cast<MockingboardSaveState_t*>(blob.data())
        ->chips[0]
        .envelope_step = 200;
    CHECK(load_and_render(blob)->chips[0].envelope_step == (200 & 0x0F));
  }

  SUBCASE("A zero noise shift register is restarted") {
    std::vector<uint8_t> blob = base;
    reinterpret_cast<MockingboardSaveState_t*>(blob.data())->chips[0].rng = 0;
    CHECK(load_and_render(blob)->chips[0].rng == 1);
  }

  SUBCASE("A carry larger than one AY tick is reduced") {
    std::vector<uint8_t> blob = base;
    reinterpret_cast<MockingboardSaveState_t*>(blob.data())->psg_remainder =
        0xFFFFFFFF;
    CHECK(load_and_render(blob)->psg_remainder < CYCLES_PER_TICK);
  }

  SUBCASE("An over-wide AY register is masked to its data-sheet width") {
    std::vector<uint8_t> blob = base;
    reinterpret_cast<MockingboardSaveState_t*>(blob.data())
        ->chips[0]
        .ay_regs[1] = 0xFF;
    CHECK(load_and_render(blob)->chips[0].ay_regs[1] == 0x0F);
  }
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

// A save taken from the card as it stood before it was rewritten, captured by
// driving this exact script through a build of the previous revision with the
// harness clock pinned, because that revision's think() read wall time:
//
//   VIA A: DDRB=0xFF DDRA=0xFF ACR=0x40 PCR=0x1C SR=0x5A IER=0xC0
//          T2C-L=0x34 T2C-H=0x12 T1C-L=0x10 T1C-H=0x27
//   VIA B: DDRB=0x0F DDRA=0xF0 ACR=0x00 PCR=0x03 SR=0xA5 IER=0xA0
//   AY0 registers 0..13 = 34 02 78 05 BC 09 17 38 0F 0A 05 40 01 0A
//   AY1 registers 0..13 = 21 01 43 03 65 07 0B 07 0C 09 06 20 02 08
//
// Some of its bytes are a host's cycle counters and one is a C++ double, so
// the only thing that can be asserted about it is that the registers come out
// the other side; a re-save is a different layout of meaning in the same 232
// bytes and would never match.
constexpr std::array<uint8_t, 232> pre_rewrite_v1_state = {
    {0x01, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x04, 0x0A, 0xFF, 0xFF,
     0x10, 0x27, 0x10, 0x27, 0x34, 0x12, 0x34, 0x12, 0x5A, 0x40, 0x1C, 0x00,
     0x40, 0x00, 0x00, 0x00, 0x34, 0x02, 0x78, 0x05, 0xBC, 0x09, 0x17, 0x38,
     0x0F, 0x0A, 0x05, 0x40, 0x01, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x04, 0x08, 0x0F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0xA5, 0x00, 0x03, 0x00, 0x20, 0x00, 0x00, 0x00, 0x21, 0x01, 0x43, 0x03,
     0x65, 0x07, 0x0B, 0x07, 0x0C, 0x09, 0x06, 0x20, 0x02, 0x08, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x01, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00}};

TEST_CASE("Mockingboard Peripheral: MB-28 A Pre-Rewrite State Still Loads") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  REQUIRE(harness.load_state(pre_rewrite_v1_state.data(),
                             pre_rewrite_v1_state.size()) == peripheral_ok);

  CHECK(harness.read_cx(VIA_A + REG_ORA) == 0x0A);
  CHECK(harness.read_cx(VIA_A + REG_ORB) == 0x04);
  CHECK(harness.read_cx(VIA_A + REG_DDRB) == 0xFF);
  CHECK(harness.read_cx(VIA_A + REG_DDRA) == 0xFF);
  CHECK(harness.read_cx(VIA_A + 0x6) == 0x10);
  CHECK(harness.read_cx(VIA_A + 0x7) == 0x27);
  CHECK(harness.read_cx(VIA_A + REG_T1C_H) == 0x27);
  CHECK(harness.read_cx(VIA_A + REG_T2C_L) == 0x34);
  CHECK(harness.read_cx(VIA_A + REG_T2C_H) == 0x12);
  CHECK(harness.read_cx(VIA_A + 0xA) == 0x5A);
  CHECK(harness.read_cx(VIA_A + REG_ACR) == 0x40);
  CHECK(harness.read_cx(VIA_A + 0xC) == 0x1C);
  CHECK(harness.read_cx(VIA_A + REG_IER) == 0xC0);

  CHECK(harness.read_cx(VIA_B + REG_DDRB) == 0x0F);
  CHECK(harness.read_cx(VIA_B + REG_DDRA) == 0xF0);
  CHECK(harness.read_cx(VIA_B + 0xA) == 0xA5);
  CHECK(harness.read_cx(VIA_B + REG_ACR) == 0x00);
  CHECK(harness.read_cx(VIA_B + 0xC) == 0x03);
  CHECK(harness.read_cx(VIA_B + REG_IER) == 0xA0);

  // Read the PSG registers back off the chips through the card's own bus, not
  // out of a save: what matters is that the loaded state reached the chip.
  const uint8_t ay0[14] = {0x34, 0x02, 0x78, 0x05, 0xBC, 0x09, 0x17,
                           0x38, 0x0F, 0x0A, 0x05, 0x40, 0x01, 0x0A};
  const uint8_t ay1[14] = {0x21, 0x01, 0x43, 0x03, 0x65, 0x07, 0x0B,
                           0x07, 0x0C, 0x09, 0x06, 0x20, 0x02, 0x08};

  harness.write_cx(VIA_A + REG_DDRA, 0x00);
  harness.write_cx(VIA_B + REG_DDRA, 0x00);
  for (uint8_t reg = 0; reg < 14; ++reg) {
    harness.write_cx(VIA_A + REG_ORA, reg);
    harness.write_cx(VIA_A + REG_ORB, ORB_LATCH);
    harness.write_cx(VIA_A + REG_ORB, ORB_READ);
    CHECK(harness.read_cx(VIA_A + REG_ORA) == ay0[reg]);

    harness.write_cx(VIA_B + REG_ORA, reg);
    harness.write_cx(VIA_B + REG_ORB, ORB_LATCH);
    harness.write_cx(VIA_B + REG_ORB, ORB_READ);
    CHECK(harness.read_cx(VIA_B + REG_ORA) == ay1[reg]);
  }

  // The old layout's cycle counter sat where the AY tick carry now does, so a
  // pre-rewrite blob is the case that proves the carry is reduced on load.
  size_t state_size = sizeof(MockingboardSaveState_t);
  std::vector<uint8_t> resaved(state_size);
  REQUIRE(harness.save_state(resaved.data(), &state_size) == peripheral_ok);
  CHECK(reinterpret_cast<const MockingboardSaveState_t*>(resaved.data())
            ->psg_remainder == 0);
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
  const Peripheral_t* desc = mockingboard_descriptor();

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

TEST_CASE("Mockingboard Peripheral: MB-37 A Rewound Slice Mark Charges Nothing") {
  constexpr uint16_t latch = 0x1000;
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  arm_timer1(harness, VIA_A, latch);

  // The counter holds its load for one cycle, so at executed_cycles t it reads
  // latch - (t - 1). Reading at 10 after reading at 18 is time running
  // backwards: it must cost nothing, and the read at 26 must still be 26
  // cycles from the arm rather than 18 + 16.
  const uint8_t at_18 = harness.read_cx(VIA_A + REG_T1C_L, 18);
  const uint8_t rewound = harness.read_cx(VIA_A + REG_T1C_L, 10);
  const uint8_t at_26 = harness.read_cx(VIA_A + REG_T1C_L, 26);

  CHECK(at_18 == static_cast<uint8_t>(latch - 17));
  CHECK(rewound == at_18);
  CHECK(at_26 == static_cast<uint8_t>(latch - 25));
}

TEST_CASE("Mockingboard Peripheral: MB-38 A Zero-Cycle Slice Changes Nothing") {
  constexpr uint32_t slice = 800;
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  set_voice_a_tone(harness, 0x00FE);
  arm_timer1(harness, VIA_A, 0x0100, ACR_FREE_RUN);
  harness.think(4096);

  size_t size = 0;
  REQUIRE(harness.save_state(nullptr, &size) == peripheral_ok);
  std::vector<uint8_t> before(size);
  size_t before_size = size;
  REQUIRE(harness.save_state(before.data(), &before_size) == peripheral_ok);

  harness.clear_audio();
  harness.think(0);
  harness.think(0);

  std::vector<uint8_t> after(size);
  size_t after_size = size;
  REQUIRE(harness.save_state(after.data(), &after_size) == peripheral_ok);

  CHECK(harness.push_count() == 0);
  CHECK(before == after);

  // A save state carries neither the coupling filter nor the cycle carry, so
  // the proof that nothing moved is that the next slice renders what it would
  // have rendered had the empty ones never happened.
  harness.think(slice);
  const std::vector<float> after_empty_slices = harness.channel(0);

  harness.reset();
  open_ay_ports(harness, VIA_A);
  set_voice_a_tone(harness, 0x00FE);
  arm_timer1(harness, VIA_A, 0x0100, ACR_FREE_RUN);
  harness.think(4096);
  harness.clear_audio();
  harness.think(slice);

  REQUIRE(after_empty_slices.size() == slice / CYCLES_PER_TICK);
  CHECK(harness.channel(0) == after_empty_slices);
}

TEST_CASE("Mockingboard Peripheral: MB-39 A Frame At Maximum Speed Is Seamless") {
  // One frame at emulation_speed_max, the longest slice the core ever hands a
  // peripheral, and 84 scratch chunks of it.
  constexpr uint32_t max_speed_frame = 681200;
  constexpr size_t ticks_per_frame = max_speed_frame / CYCLES_PER_TICK;
  constexpr size_t chunks_per_frame = 84;

  // A voice out of reset sits at its low half period, and a slice short enough
  // to fall entirely inside that is silence and is not pushed. Both runs start
  // past it, and on a whole tick, so the two are comparable sample for sample.
  constexpr uint32_t warm_up = 4096;

  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  set_voice_a_tone(harness, 0x00FE);
  harness.think(warm_up);
  harness.clear_audio();
  harness.think(max_speed_frame);

  CHECK(harness.total_pushed_samples() == ticks_per_frame);
  CHECK(harness.push_count() == chunks_per_frame);
  const std::vector<float> unbroken = harness.channel(0);
  REQUIRE(unbroken.size() == ticks_per_frame);

  harness.reset();
  open_ay_ports(harness, VIA_A);
  set_voice_a_tone(harness, 0x00FE);
  harness.think(warm_up);
  harness.clear_audio();

  // The same cycles split so that no piece is a whole number of chunks and
  // every piece leaves a different carry behind.
  harness.think(7);
  harness.think(101);
  harness.think(max_speed_frame - 7 - 101 - 3);
  harness.think(3);

  CHECK(harness.total_pushed_samples() == ticks_per_frame);
  CHECK(harness.channel(0) == unbroken);
}

TEST_CASE("Mockingboard Peripheral: MB-40 A Slice Past The Counter's Width") {
  // Two full counter widths and three cycles: the counter wraps twice inside
  // one slice. The free-run period of 4098 shares only a factor of two with
  // the 8192-cycle scratch chunk, so across this slice an underflow lands at
  // every even offset within a chunk.
  constexpr uint32_t long_slice = (0xFFFFU * 2U) + 3U;
  constexpr uint16_t latch = 0x1000;
  // 131073 cycles is 31 whole 4098-cycle periods and 4035 cycles of the
  // thirty-second. One of those 4035 goes on the reload, leaving the counter
  // at 4096 - 4034. And 131073 cycles is 16384 whole AY ticks.
  constexpr size_t ticks_in_slice = 16384;
  constexpr uint8_t counter_low = 62;

  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  set_voice_a_tone(harness, 0x00FE);
  arm_timer1(harness, VIA_A, latch, ACR_FREE_RUN);
  harness.clear_audio();

  harness.think(long_slice);

  CHECK(harness.total_pushed_samples() == ticks_in_slice);
  CHECK(harness.irq_asserted());
  CHECK(harness.read_cx(VIA_A + REG_IFR) == (IFR_T1 | 0x80));
  CHECK(harness.read_cx(VIA_A + REG_T1C_H) == 0x00);
  CHECK(harness.read_cx(VIA_A + REG_T1C_L) == counter_low);
}
TEST_CASE("Mockingboard Peripheral: MB-41 A State Of Nothing But Ones") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  size_t state_size = 0;
  REQUIRE(harness.save_state(nullptr, &state_size) == peripheral_ok);

  std::vector<uint8_t> blob(state_size, 0xFF);
  auto* ss = reinterpret_cast<MockingboardSaveState_t*>(blob.data());
  ss->version = MOCKINGBOARD_STATE_VERSION;
  ss->struct_size = static_cast<uint32_t>(state_size);

  // A length nobody could have written is rejected before the body is read.
  CHECK(harness.load_state(blob.data(), SIZE_MAX) == peripheral_error);

  // Every VIA and AY register accepts any byte, so once the header is right
  // there is nothing here to reject -- the load has to succeed and the next
  // slice has to render inside the peak magnitude the card declares.
  REQUIRE(harness.load_state(blob.data(), state_size) == peripheral_ok);
  harness.clear_audio();
  harness.think(1024);

  REQUIRE(harness.push_count() > 0);
  bool within_peak = true;
  for (size_t v = 0; v < MB_VOICES; ++v) {
    for (const float sample : harness.channel(v)) {
      within_peak = within_peak && (sample >= -1.0F) && (sample <= 1.0F);
    }
  }
  CHECK(within_peak);
}

TEST_CASE("Mockingboard Peripheral: MB-42 The AY Bus Ignores What It Cannot Do") {
  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);

  open_ay_ports(harness, VIA_A);
  write_ay(harness, VIA_A, 0x02, 0xC3);

  // A9 and A8 are grounded, so an address above fifteen selects no chip and
  // the previous register stays latched: the write that follows lands there.
  write_ay(harness, VIA_A, 0x20, 0x55);

  // A read strobe while the 6502 is still driving port A changes nothing,
  // because the chip cannot pull against it.
  harness.write_cx(VIA_A + REG_ORA, 0x02);
  harness.write_cx(VIA_A + REG_ORB, ORB_LATCH);
  harness.write_cx(VIA_A + REG_ORB, ORB_INACTIVE);
  harness.write_cx(VIA_A + REG_ORA, 0x77);
  harness.write_cx(VIA_A + REG_ORB, ORB_READ);
  CHECK(harness.read_cx(VIA_A + REG_ORA) == 0x77);

  size_t state_size = 0;
  REQUIRE(harness.save_state(nullptr, &state_size) == peripheral_ok);
  std::vector<uint8_t> buffer(state_size);
  REQUIRE(harness.save_state(buffer.data(), &state_size) == peripheral_ok);
  const auto* ss =
      reinterpret_cast<const MockingboardSaveState_t*>(buffer.data());
  CHECK(ss->chips[0].ay_regs[2] == 0x55);
}

TEST_CASE("Mockingboard Peripheral: MB-33 One Emulated Minute Of Rendering") {
  // What the core pays per frame when it is rendering and throwing the result
  // away, which is what warp does: the audio sink is gone but the card still
  // has to run both PSGs and both VIAs. Four thousand frames is one emulated
  // minute; multiplying the figure this prints by the maximum emulation speed
  // gives the cost per wall second at the top of the warp range.
  constexpr uint32_t frames = 4000;
  constexpr double frames_per_emulated_second = 60.0;

  MockingboardHarness harness;
  REQUIRE(harness.create_card(4) != nullptr);
  harness.disable_audio_push();

  open_ay_ports(harness, VIA_A);
  set_voice_a_tone(harness, 0x00FE);
  arm_timer1(harness, VIA_A, 0x1000, ACR_FREE_RUN);
  harness.clear_audio();

  const auto started = std::chrono::steady_clock::now();
  for (uint32_t frame = 0; frame < frames; ++frame) {
    harness.think(NTSC_FRAME_CYCLES);
  }
  const double wall_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  MESSAGE("milliseconds of wall clock per emulated second: "
          << (wall_seconds * frames_per_emulated_second * 1000.0) /
                 static_cast<double>(frames));
  CHECK(harness.push_count() == 0);
  CHECK(harness.irq_asserted());
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
