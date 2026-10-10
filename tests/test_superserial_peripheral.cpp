// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include "HeadlessHarness.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"
#include "test_superserial_abi_c.h"

namespace {

// Through the registry, so one binary covers the built-in card and a loaded
// plugin alike.
auto serial_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.ssc");
}

constexpr int test_slot = 2;
constexpr size_t frame_size = 56;
constexpr uint16_t program_start = 0x0300;
constexpr uint16_t handler_start = 0x0380;
constexpr uint32_t cycle_cap = 20000;
// Ten bits at 9600 baud on an NTSC clock of 1,020,484.45 Hz, and the 9/16
// stop-bit point RDRF is set at.
constexpr uint64_t char_9600_8n1 = 1063;
constexpr uint64_t rdrf_9600_8n1 = 1016;
constexpr uint64_t char_300_8n1 = 34016;
// LDX #0 / DEX / BNE: 2 + 256 x 2 + 255 x 3 + 2, more than one character
// time at 9600 baud.
constexpr uint32_t delay_cycles = 1281;
// The longest 6502 instruction: the latest an event lands past its cycle.
constexpr uint64_t one_instruction = 7;
constexpr uint8_t register_data = 0x8;
constexpr uint8_t register_status = 0x9;
constexpr uint8_t register_command = 0xA;
constexpr uint8_t register_control = 0xB;
constexpr uint8_t register_switches_1 = 0x1;
constexpr uint8_t register_switches_2 = 0x2;
constexpr uint8_t control_9600_8n1 = 0x1E;
constexpr uint8_t control_300_8n1 = 0x16;
constexpr uint8_t command_dtr_rx_irq = 0x09;
constexpr uint8_t command_dtr_tx_irq = 0x05;
constexpr uint8_t command_dtr_only = 0x01;
constexpr uint8_t command_firmware = 0x0B;
constexpr uint8_t command_break = 0x0F;

auto card_address(int slot, uint8_t offset) -> uint16_t {
  return static_cast<uint16_t>(0xC080 + (slot << 4) + offset);
}

auto hex(const std::vector<uint8_t>& bytes) -> std::string {
  std::string text;
  std::array<char, 4> digits{};
  for (uint8_t byte : bytes) {
    snprintf(digits.data(), digits.size(), "%02X ", byte);
    text += digits.data();
  }
  return text;
}

template <size_t N>
auto hex(const std::array<uint8_t, N>& bytes) -> std::string {
  return hex(std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

class BenchHost_t {
 public:
  BenchHost_t() {
    host_.Log = bench_log;
    host_.AssertIrq = bench_assert_irq;
    host_.RegisterIO = bench_register_io;
    host_.RegisterCxROM = bench_register_cx_rom;
    host_.RegisterExpansionROM = bench_register_expansion_rom;
    host_.GetCycles = bench_get_cycles;
    host_.GetClockHz = bench_get_clock_hz;
    host_.ReadFloatingBus = bench_read_floating_bus;
    host_.SinkOpen = bench_sink_open;
    host_.SinkWrite = bench_sink_write;
    host_.SinkReady = bench_sink_ready;
    host_.SinkClose = bench_sink_close;
    host_.SinkRead = bench_sink_read;
    host_.SinkSetLine = bench_sink_set_line;
    host_.SinkGetLines = bench_sink_get_lines;
    host_.ScheduleEvent = bench_schedule_event;
    s_last_log.clear();
    s_read_c0 = nullptr;
  }

  auto host() -> HostInterface_t* { return &host_; }
  static auto last_log() -> const std::string& { return s_last_log; }
  static auto read_c0(void* instance, uint8_t offset) -> uint8_t {
    REQUIRE(s_read_c0 != nullptr);
    return s_read_c0(instance, 0, card_address(test_slot, offset), 0, 0, 0);
  }

 private:
  static auto bench_log(void* instance, PeripheralLogLevel_t level,
                        const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    std::array<char, 256> line{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(line.data(), line.size(), fmt, args);
    va_end(args);
    s_last_log = line.data();
  }
  static auto bench_assert_irq(int slot, bool assert) -> void {
    (void)slot;
    (void)assert;
  }
  static auto bench_register_io(int slot, PeripheralIOHandler read_c0,
                                PeripheralIOHandler write_c0,
                                PeripheralIOHandler read_cx,
                                PeripheralIOHandler write_cx) -> void {
    (void)slot;
    (void)write_c0;
    (void)read_cx;
    (void)write_cx;
    s_read_c0 = read_c0;
  }
  static auto bench_register_cx_rom(int slot, const uint8_t* rom) -> void {
    (void)slot;
    (void)rom;
  }
  static auto bench_register_expansion_rom(int slot, const uint8_t* rom)
      -> void {
    (void)slot;
    (void)rom;
  }
  static auto bench_get_cycles() -> uint64_t { return 0; }
  static auto bench_get_clock_hz() -> double { return 1020484.45; }
  static auto bench_read_floating_bus(uint32_t executed_cycles) -> uint8_t {
    (void)executed_cycles;
    return 0;
  }
  static auto bench_sink_open(void* instance, int slot,
                              PeripheralSinkKind_t kind) -> void* {
    (void)instance;
    (void)slot;
    (void)kind;
    return &s_token;
  }
  static auto bench_sink_write(void* sink, uint8_t byte) -> void {
    (void)sink;
    (void)byte;
  }
  static auto bench_sink_ready(void* sink) -> bool {
    (void)sink;
    return false;
  }
  static auto bench_sink_close(void* sink) -> void { (void)sink; }
  static auto bench_sink_read(void* /*sink*/, uint8_t* /*byte*/) -> bool {
    return false;
  }
  static auto bench_sink_set_line(void* sink,
                                  const PeripheralSerialLine_t* line) -> void {
    (void)sink;
    (void)line;
  }
  static auto bench_sink_get_lines(void* /*sink*/, uint8_t* /*lines*/) -> bool {
    return false;
  }
  static auto bench_schedule_event(void* instance, uint64_t at_cycle) -> void {
    (void)instance;
    (void)at_cycle;
  }

  static int s_token;
  static std::string s_last_log;
  static PeripheralIOHandler s_read_c0;
  HostInterface_t host_{};
};

int BenchHost_t::s_token = 0;
std::string BenchHost_t::s_last_log;
PeripheralIOHandler BenchHost_t::s_read_c0 = nullptr;

class BenchCard_t {
 public:
  explicit BenchCard_t(int slot = test_slot)
      : instance_(serial_descriptor()->init(slot, host_.host())) {}
  ~BenchCard_t() {
    if (instance_ != nullptr) {
      serial_descriptor()->shutdown(instance_);
    }
  }
  BenchCard_t(const BenchCard_t&) = delete;
  auto operator=(const BenchCard_t&) -> BenchCard_t& = delete;
  BenchCard_t(BenchCard_t&&) = delete;
  auto operator=(BenchCard_t&&) -> BenchCard_t& = delete;

  auto instance() const -> void* { return instance_; }

 private:
  BenchHost_t host_;
  void* instance_;
};

// Assembled rather than written as byte arrays because the programs address
// a card by its slot.
struct Program_t {
  uint16_t origin;
  std::vector<uint8_t> bytes;

  explicit Program_t(uint16_t at = program_start) : origin(at) {}

  auto here() const -> uint16_t {
    return static_cast<uint16_t>(origin + bytes.size());
  }
  auto emit(uint8_t opcode) -> void { bytes.push_back(opcode); }
  auto emit(uint8_t opcode, uint8_t operand) -> void {
    bytes.push_back(opcode);
    bytes.push_back(operand);
  }
  auto emit(uint8_t opcode, uint16_t operand) -> void {
    bytes.push_back(opcode);
    bytes.push_back(static_cast<uint8_t>(operand & 0xFF));
    bytes.push_back(static_cast<uint8_t>(operand >> 8));
  }
  auto branch(uint8_t opcode, uint16_t target) -> void {
    const auto offset =
        static_cast<int>(target) - (static_cast<int>(here()) + 2);
    REQUIRE(offset >= -128);
    REQUIRE(offset <= 127);
    emit(opcode, static_cast<uint8_t>(offset & 0xFF));
  }

  auto lda_imm(uint8_t v) -> void { emit(0xA9, v); }
  auto ldx_imm(uint8_t v) -> void { emit(0xA2, v); }
  auto ldy_imm(uint8_t v) -> void { emit(0xA0, v); }
  auto lda_zp(uint8_t a) -> void { emit(0xA5, a); }
  auto ldx_zp(uint8_t a) -> void { emit(0xA6, a); }
  auto sta_zp(uint8_t a) -> void { emit(0x85, a); }
  auto stx_zp(uint8_t a) -> void { emit(0x86, a); }
  auto inc_zp(uint8_t a) -> void { emit(0xE6, a); }
  auto lda_abs(uint16_t a) -> void { emit(0xAD, a); }
  auto sta_abs(uint16_t a) -> void { emit(0x8D, a); }
  auto stx_abs(uint16_t a) -> void { emit(0x8E, a); }
  auto sty_abs(uint16_t a) -> void { emit(0x8C, a); }
  auto sta_abs_x(uint16_t a) -> void { emit(0x9D, a); }
  auto jmp(uint16_t a) -> void { emit(0x4C, a); }
  auto jsr(uint16_t a) -> void { emit(0x20, a); }
  auto bne(uint16_t a) -> void { branch(0xD0, a); }
  auto bcs(uint16_t a) -> void { branch(0xB0, a); }
  auto adc_imm(uint8_t v) -> void { emit(0x69, v); }
  auto cpx_imm(uint8_t v) -> void { emit(0xE0, v); }
  auto and_imm(uint8_t v) -> void { emit(0x29, v); }
  auto beq(uint16_t a) -> void { branch(0xF0, a); }
  auto dex() -> void { emit(0xCA); }
  auto txa() -> void { emit(0x8A); }
  auto clc() -> void { emit(0x18); }
  auto cli() -> void { emit(0x58); }
  auto php() -> void { emit(0x08); }
  auto pla() -> void { emit(0x68); }
  auto rti() -> void { emit(0x40); }

  auto spin() -> uint16_t {
    const uint16_t at = here();
    jmp(at);
    return at;
  }
  auto delay() -> void {
    ldx_imm(0);
    const uint16_t loop = here();
    dex();
    bne(loop);
  }
  auto lda_card(int slot, uint8_t offset) -> void {
    lda_abs(card_address(slot, offset));
  }
  auto sta_card(int slot, uint8_t offset) -> void {
    sta_abs(card_address(slot, offset));
  }
  auto write_card(int slot, uint8_t offset, uint8_t value) -> void {
    lda_imm(value);
    sta_card(slot, offset);
  }
  auto read_card_into(int slot, uint8_t offset, uint8_t zero_page) -> void {
    lda_card(slot, offset);
    sta_zp(zero_page);
  }
  auto program_acia(int slot, uint8_t control, uint8_t command) -> void {
    write_card(slot, register_control, control);
    write_card(slot, register_command, command);
  }

  auto poke() const -> void {
    TestFixtures::ScopedCore_t::poke(origin, bytes.data(), bytes.size());
  }
};

auto poke_irq_vector(uint16_t handler) -> void {
  const std::array<uint8_t, 2> vector = {static_cast<uint8_t>(handler & 0xFF),
                                         static_cast<uint8_t>(handler >> 8)};
  TestFixtures::ScopedCore_t::poke(IRQ_VECTOR_ADDR, vector);
}

// $06 counts the entries; the register read is what acknowledges, or fails
// to acknowledge, the card.
auto poke_counting_handler(int slot, uint8_t acknowledging_register) -> void {
  Program_t handler(handler_start);
  handler.inc_zp(0x06);
  handler.lda_card(slot, acknowledging_register);
  handler.rti();
  handler.poke();
  poke_irq_vector(handler_start);
}

auto zero_page_clear(uint8_t from, uint8_t to) -> void {
  for (uint8_t a = from; a <= to; ++a) {
    const std::array<uint8_t, 1> zero = {0};
    TestFixtures::ScopedCore_t::poke(a, zero);
  }
}

auto describe_slots(const std::vector<int>& slots)
    -> TestFixtures::ScopedTestConfig_t::Description_t {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  for (int slot : slots) {
    description.slots.at(static_cast<size_t>(slot - 1)) = "Super Serial Card";
  }
  return description;
}

// The sink is the first member so it is installed before the core builds the
// card and still there when the card's shutdown closes it.
struct SerialMachine_t {
  TestFixtures::ScopedByteSink_t sink;
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;
  int slot;

  explicit SerialMachine_t(int in_slot = test_slot)
      : SerialMachine_t(std::vector<int>{in_slot}) {}
  explicit SerialMachine_t(const std::vector<int>& slots)
      : config(describe_slots(slots)), core(config), slot(slots.front()) {
    peripheral_manager_init();
    linapple_register_peripherals();
    linapple_reset_hard();
  }

  static auto run_until(uint16_t entry, uint16_t sentinel,
                        uint32_t cap = cycle_cap) -> uint32_t {
    TestFixtures::enter_at({entry, 0, 0, 0});
    const uint32_t cycles = TestFixtures::step_until_pc(sentinel, cap);
    REQUIRE(cpu_get_registers()->pc == sentinel);
    return cycles;
  }

  static auto run_cycles(uint16_t entry, uint32_t cycles) -> void {
    TestFixtures::enter_at({entry, 0, 0, 0});
    uint32_t ran = 0;
    while (ran < cycles) {
      ran += cpu_execute(0);
    }
  }

  auto stream() const -> std::vector<uint8_t> {
    std::vector<uint8_t> out;
    for (const TestFixtures::ScopedByteSink_t::Byte_t& entry : sink.bytes()) {
      if (entry.slot == slot) {
        out.push_back(entry.byte);
      }
    }
    return out;
  }
};

// Into $10-$13.
auto read_registers(SerialMachine_t& machine) -> void {
  Program_t program;
  program.read_card_into(machine.slot, register_status, 0x10);
  program.read_card_into(machine.slot, register_control, 0x11);
  program.read_card_into(machine.slot, register_command, 0x12);
  program.read_card_into(machine.slot, register_data, 0x13);
  const uint16_t spin = program.spin();
  program.poke();
  machine.run_until(program_start, spin);
}

// $C0n1 advances the chip without touching the ACIA: a status read would
// clear the interrupt, a data read RDRF.
auto touch_card(SerialMachine_t& machine, bool after_delay) -> void {
  Program_t program;
  if (after_delay) {
    program.delay();
  }
  program.lda_card(machine.slot, register_switches_1);
  const uint16_t spin = program.spin();
  program.poke();
  machine.run_until(program_start, spin);
}

using Frame_t = std::array<uint8_t, frame_size>;
constexpr size_t frame_rx_count = 8;
constexpr size_t frame_control = 12;
constexpr size_t frame_command = 13;
constexpr size_t frame_irq = 14;
constexpr size_t frame_latches = 27;
constexpr size_t frame_receive_data = 52;
constexpr size_t frame_transmit_data = 53;
constexpr size_t frame_shift_data = 54;
constexpr uint8_t latch_rdrf = 0x08;
constexpr uint8_t latch_overrun = 0x04;
constexpr uint8_t latch_tdr_full = 0x10;
constexpr uint8_t latch_tx_busy = 0x20;
constexpr uint8_t latch_rx_busy = 0x40;

auto save_frame(int slot) -> Frame_t {
  Frame_t frame{};
  size_t size = frame.size();
  peripheral_save_state(slot, frame.data(), &size);
  REQUIRE(size == frame.size());
  return frame;
}

auto frame_header() -> Frame_t {
  Frame_t frame{};
  frame.at(0) = 0x01;
  frame.at(4) = 0x38;
  return frame;
}

auto load_frame(int slot, const Frame_t& frame) -> void {
  REQUIRE(peripheral_load_state(slot, frame.data(), frame.size()) ==
          peripheral_ok);
}

// The sink guard follows the harness so it takes over whatever sink the
// controller installed. The controller's frame loop is the one path on which
// the card's wake-ups cut the frame.
struct SerialSession_t {
  TestFixtures::ScopedTestConfig_t config;
  HeadlessHarness_t harness;
  TestFixtures::ScopedByteSink_t sink;
  int slot;

  explicit SerialSession_t(int in_slot = test_slot)
      : config(describe_slots({in_slot})), harness(config), slot(in_slot) {}

  // No disk controller, so the Autostart scan falls through to Applesoft.
  auto boot_to_prompt() -> void {
    harness.boot();
    constexpr uint32_t prompt_frame_cap = 300;
    bool at_prompt = false;
    uint32_t frames = 0;
    while (!at_prompt && frames < prompt_frame_cap) {
      harness.run_frames(1);
      ++frames;
      for (int row = 0; row < 24; ++row) {
        if (harness.get_text_row(row) == "]") {
          at_prompt = true;
        }
      }
    }
    CAPTURE(frames);
    REQUIRE(at_prompt);
  }

  auto stream() const -> std::vector<uint8_t> {
    std::vector<uint8_t> out;
    for (const TestFixtures::ScopedByteSink_t::Byte_t& entry : sink.bytes()) {
      CHECK(entry.slot == slot);
      out.push_back(entry.byte);
    }
    return out;
  }

  auto screen_has_row(const std::string& text) const -> bool {
    for (int row = 0; row < 24; ++row) {
      if (harness.get_text_row(row) == text) {
        return true;
      }
    }
    return false;
  }
};

}  // namespace

TEST_CASE("Super Serial Card: the registry resolves the card by id and name") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  Peripheral_t* card = serial_descriptor();
  REQUIRE(card != nullptr);
  CHECK(peripheral_find_internal("Super Serial Card") == card);
  CHECK(card->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(card->id) == "linapple.ssc");
  CHECK(std::string(card->name) == "Super Serial Card");
  CHECK(card->compatible_slots == PERIPHERAL_MASK_EXPANSION);
  CHECK(card->default_slot == 2);
  CHECK(card->init != nullptr);
  CHECK(card->reset != nullptr);
  CHECK(card->shutdown != nullptr);
  CHECK(card->think != nullptr);
  CHECK(card->save_state != nullptr);
  CHECK(card->load_state != nullptr);
  CHECK(card->command != nullptr);
  CHECK(card->query != nullptr);
}

TEST_CASE(
    "Super Serial Card: init refuses a NULL host and the other entries take a "
    "NULL instance in their stride") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  Peripheral_t* card = serial_descriptor();
  REQUIRE(card != nullptr);

  CHECK(card->init(test_slot, nullptr) == nullptr);
  card->reset(nullptr);
  card->shutdown(nullptr);
  card->think(nullptr, 0);
  size_t size = 0;
  CHECK(card->save_state(nullptr, nullptr, &size) == peripheral_ok);
  CHECK(card->load_state(nullptr, &size, sizeof(size)) == peripheral_error);

  BenchCard_t bench;
  CHECK(bench.instance() != nullptr);
}

TEST_CASE(
    "Super Serial Card: a command or query from another subsystem is "
    "incompatible") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  Peripheral_t* card = serial_descriptor();
  REQUIRE(card != nullptr);
  BenchCard_t bench;
  REQUIRE(bench.instance() != nullptr);

  uint8_t byte = 0x55;
  CHECK(card->command(bench.instance(), PERIPHERAL_SUBSYSTEM_DISK | 0x0001,
                      &byte, sizeof(byte)) == peripheral_incompatible);
  CHECK(card->command(bench.instance(), PERIPHERAL_SUBSYSTEM_PRINTER | 0x0002,
                      nullptr, 0) == peripheral_incompatible);
  size_t size = sizeof(byte);
  CHECK(card->query(bench.instance(), PERIPHERAL_SUBSYSTEM_DISK | 0x0001, &byte,
                    &size) == peripheral_incompatible);
  CHECK(card->query(bench.instance(), PERIPHERAL_SUBSYSTEM_CLOCK | 0x0001,
                    &byte, &size) == peripheral_incompatible);
}

TEST_CASE(
    "Super Serial Card: save_state answers the sizing probe with the 56-byte "
    "frame C sees") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  Peripheral_t* card = serial_descriptor();
  REQUIRE(card != nullptr);
  BenchCard_t bench;
  REQUIRE(bench.instance() != nullptr);

  size_t size = 0;
  REQUIRE(card->save_state(bench.instance(), nullptr, &size) == peripheral_ok);
  CHECK(size == frame_size);
  CHECK(superserial_abi_c_frame_size() == frame_size);
  CHECK(superserial_abi_c_state_version() == 1);

  std::array<uint8_t, frame_size - 1> too_small{};
  size = too_small.size();
  CHECK(card->save_state(bench.instance(), too_small.data(), &size) ==
        peripheral_error);
}

namespace {

struct RequiredMember_t {
  const char* name;
  void (*clear)(HostInterface_t*);
};

const std::array<RequiredMember_t, 14> required_members = {{
    {"AssertIrq", [](HostInterface_t* h) { h->AssertIrq = nullptr; }},
    {"RegisterIO", [](HostInterface_t* h) { h->RegisterIO = nullptr; }},
    {"RegisterCxROM", [](HostInterface_t* h) { h->RegisterCxROM = nullptr; }},
    {"RegisterExpansionROM",
     [](HostInterface_t* h) { h->RegisterExpansionROM = nullptr; }},
    {"GetCycles", [](HostInterface_t* h) { h->GetCycles = nullptr; }},
    {"GetClockHz", [](HostInterface_t* h) { h->GetClockHz = nullptr; }},
    {"ReadFloatingBus",
     [](HostInterface_t* h) { h->ReadFloatingBus = nullptr; }},
    {"SinkOpen", [](HostInterface_t* h) { h->SinkOpen = nullptr; }},
    {"SinkWrite", [](HostInterface_t* h) { h->SinkWrite = nullptr; }},
    {"SinkClose", [](HostInterface_t* h) { h->SinkClose = nullptr; }},
    {"SinkRead", [](HostInterface_t* h) { h->SinkRead = nullptr; }},
    {"SinkSetLine", [](HostInterface_t* h) { h->SinkSetLine = nullptr; }},
    {"SinkGetLines", [](HostInterface_t* h) { h->SinkGetLines = nullptr; }},
    {"ScheduleEvent", [](HostInterface_t* h) { h->ScheduleEvent = nullptr; }},
}};

}  // namespace

TEST_CASE(
    "Super Serial Card: init refuses a host lacking any member it needs by "
    "name, a mute host, and a slot outside 1 to 7") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  Peripheral_t* card = serial_descriptor();
  REQUIRE(card != nullptr);
  BenchHost_t bench;

  for (const RequiredMember_t& member : required_members) {
    CAPTURE(member.name);
    HostInterface_t partial = *bench.host();
    member.clear(&partial);
    CHECK(card->init(test_slot, &partial) == nullptr);
    CHECK(BenchHost_t::last_log().find(member.name) != std::string::npos);
    CHECK(BenchHost_t::last_log().find("slot 2") != std::string::npos);
  }

  HostInterface_t mute = *bench.host();
  mute.Log = nullptr;
  CHECK(card->init(test_slot, &mute) == nullptr);

  CHECK(card->init(0, bench.host()) == nullptr);
  CHECK(BenchHost_t::last_log().find("slot 0") != std::string::npos);
  CHECK(card->init(8, bench.host()) == nullptr);
  CHECK(BenchHost_t::last_log().find("slot 8") != std::string::npos);
  void* instance = card->init(7, bench.host());
  REQUIRE(instance != nullptr);
  card->shutdown(instance);
}

TEST_CASE(
    "Super Serial Card: the switch command takes exactly two bytes with bit 7 "
    "clear and leaves the image alone otherwise, and no query is answered") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  Peripheral_t* card = serial_descriptor();
  REQUIRE(card != nullptr);
  BenchCard_t bench;
  REQUIRE(bench.instance() != nullptr);

  CHECK(superserial_abi_c_switches_size() == sizeof(SuperSerialSwitches_t));
  CHECK(superserial_abi_c_set_switches_id() == SUPER_SERIAL_CMD_SET_SWITCHES);
  CHECK(BenchHost_t::read_c0(bench.instance(), register_switches_1) == 0xEC);

  SuperSerialSwitches_t switches{0x78, 0x2F};
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES,
                      &switches, 1) == peripheral_error);
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES,
                      &switches, 3) == peripheral_error);
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES, nullptr,
                      sizeof(switches)) == peripheral_error);
  switches = {0xE8, 0x0B};
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES,
                      &switches, sizeof(switches)) == peripheral_error);
  switches = {0x68, 0x8B};
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES,
                      &switches, sizeof(switches)) == peripheral_error);
  CHECK(BenchHost_t::read_c0(bench.instance(), register_switches_1) == 0xEC);

  switches = {0x68, 0x0B};
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES,
                      &switches, sizeof(switches)) == peripheral_ok);
  CHECK(BenchHost_t::read_c0(bench.instance(), register_switches_1) == 0xEE);

  CHECK(card->command(bench.instance(), PERIPHERAL_SUBSYSTEM_SERIAL | 0x0001,
                      &switches, 1) == peripheral_incompatible);
  CHECK(card->command(bench.instance(), PERIPHERAL_SUBSYSTEM_SERIAL | 0x0002,
                      &switches, sizeof(switches)) == peripheral_incompatible);
  CHECK(card->command(bench.instance(), PERIPHERAL_SUBSYSTEM_SERIAL | 0x0004,
                      &switches, sizeof(switches)) == peripheral_incompatible);
  CHECK(card->command(nullptr, SUPER_SERIAL_CMD_SET_SWITCHES, &switches,
                      sizeof(switches)) == peripheral_error);

  size_t size = sizeof(switches);
  CHECK(card->query(bench.instance(), PERIPHERAL_SUBSYSTEM_SERIAL | 0x0001,
                    &switches, &size) == peripheral_incompatible);
  CHECK(card->query(bench.instance(), PERIPHERAL_SUBSYSTEM_SERIAL | 0x0002,
                    nullptr, &size) == peripheral_incompatible);
  CHECK(card->query(bench.instance(), PERIPHERAL_SUBSYSTEM_SERIAL | 0x0003,
                    &switches, nullptr) == peripheral_error);
}

namespace {

constexpr size_t rom_size = 2048;
constexpr size_t rom_slot_page = 0x700;
constexpr uint16_t slot_page_copy = 0x2000;
constexpr uint16_t expansion_copy = 0x2100;
constexpr size_t expansion_pages = 7;
constexpr uint32_t copy_cycle_cap = 60000;

using Rom_t = std::array<uint8_t, rom_size>;

auto read_rom_file() -> Rom_t {
  Rom_t rom{};
  std::ifstream in(TestFixtures::get_fixture_path("roms/SSC.rom"),
                   std::ios::binary);
  REQUIRE(in.is_open());
  in.read(reinterpret_cast<char*>(rom.data()),
          static_cast<std::streamsize>(rom.size()));
  REQUIRE(in.gcount() == static_cast<std::streamsize>(rom.size()));
  CHECK(in.peek() == std::ifstream::traits_type::eof());
  return rom;
}

// Copies the slot page to $2000 and $C800-$CEFF to $2100; $CF00 is never
// read, since a read there would reset the latch.
auto poke_rom_copier(int slot) -> uint16_t {
  const auto slot_page = static_cast<uint8_t>(0xC0 + slot);
  const std::array<uint8_t, 49> program = {
      0xA2, 0x00,                   // LDX #$00
      0xBD, 0x00, slot_page,        // LDA $Cn00,X
      0x9D, 0x00, 0x20,             // STA $2000,X
      0xE8,                         // INX
      0xD0, 0xF7,                   // BNE $0302
      0xA9, 0x00, 0x85,      0x06,  // LDA #$00 / STA $06
      0xA9, 0xC8, 0x85,      0x07,  // LDA #$C8 / STA $07
      0xA9, 0x00, 0x85,      0x08,  // LDA #$00 / STA $08
      0xA9, 0x21, 0x85,      0x09,  // LDA #$21 / STA $09
      0xA0, 0x00,                   // LDY #$00
      0xB1, 0x06,                   // LDA ($06),Y
      0x91, 0x08,                   // STA ($08),Y
      0xC8,                         // INY
      0xD0, 0xF9,                   // BNE $031D
      0xE6, 0x07,                   // INC $07
      0xE6, 0x09,                   // INC $09
      0xA5, 0x07,                   // LDA $07
      0xC9, 0xCF,                   // CMP #$CF
      0xD0, 0xEF,                   // BNE $031D
      0x4C, 0x2E, 0x03              // JMP $032E
  };
  TestFixtures::ScopedCore_t::poke(program_start, program);
  return 0x032E;
}

}  // namespace

TEST_CASE(
    "Super Serial Card: the slot page is the ROM's last page, the expansion "
    "ROM is the whole image, and both match res/roms/SSC.rom byte for byte") {
  const Rom_t rom = read_rom_file();
  SerialMachine_t machine;
  const uint16_t sentinel = poke_rom_copier(machine.slot);
  const uint32_t cycles =
      machine.run_until(program_start, sentinel, copy_cycle_cap);
  CHECK(cycles < copy_cycle_cap);

  // The $Cn00 entry, the Pascal signature bytes, the Pascal 1.1 init offset
  // and the revision byte (1981 manual pp. 55-57).
  CHECK(mem[slot_page_copy + 0x00] == 0x2C);
  CHECK(mem[slot_page_copy + 0x01] == 0x58);
  CHECK(mem[slot_page_copy + 0x02] == 0xFF);
  CHECK(mem[slot_page_copy + 0x03] == 0x70);
  CHECK(mem[slot_page_copy + 0x04] == 0x0C);
  CHECK(mem[slot_page_copy + 0x05] == 0x38);
  CHECK(mem[slot_page_copy + 0x07] == 0x18);
  CHECK(mem[slot_page_copy + 0x0B] == 0x01);
  CHECK(mem[slot_page_copy + 0x0C] == 0x31);
  CHECK(mem[slot_page_copy + 0x0D] == 0x8E);
  CHECK(mem[slot_page_copy + 0xFF] == 0x08);
  // The $C800 initialisation entry, JSR $C99B.
  CHECK(mem[expansion_copy + 0x00] == 0x20);
  CHECK(mem[expansion_copy + 0x01] == 0x9B);
  CHECK(mem[expansion_copy + 0x02] == 0xC9);

  size_t slot_page_mismatches = 0;
  for (size_t i = 0; i < 0x100; ++i) {
    if (mem[slot_page_copy + i] != rom.at(rom_slot_page + i)) {
      ++slot_page_mismatches;
    }
  }
  CHECK(slot_page_mismatches == 0);

  size_t expansion_mismatches = 0;
  for (size_t i = 0; i < expansion_pages * 0x100; ++i) {
    if (mem[expansion_copy + i] != rom.at(i)) {
      ++expansion_mismatches;
    }
  }
  CHECK(expansion_mismatches == 0);
}

// The //e's INTC8ROM flip-flop, set by a $C3xx access, holds the internal ROM
// in until a $CFFF access clears it (IIe Technical Reference p. 134), which
// is why the 80-column firmware does LDX $CFFF at $C372; the card's latch
// behaves the same. The three $C800 reads see the internal ROM's JMP $C9B0,
// the card's JSR $C99B, and the JMP again.
TEST_CASE(
    "Super Serial Card: on an Enhanced //e the $C800 space alternates between "
    "the internal ROM after a $C3xx fetch and the card's ROM after a $C2xx "
    "fetch") {
  SerialMachine_t machine;
  const auto slot_page = static_cast<uint16_t>(0xC000 + (machine.slot << 8));
  Program_t program;
  uint8_t store = 0x10;
  for (uint16_t entry : {uint16_t{0xC300}, slot_page, uint16_t{0xC300}}) {
    program.lda_abs(0xCFFF);
    program.lda_abs(entry);
    for (uint16_t address : {0xC800, 0xC801, 0xC802}) {
      program.lda_abs(address);
      program.sta_zp(store++);
    }
  }
  const uint16_t spin = program.spin();
  program.poke();
  machine.run_until(program_start, spin);

  CHECK(mem[0x10] == 0x4C);
  CHECK(mem[0x11] == 0xB0);
  CHECK(mem[0x12] == 0xC9);
  CHECK(mem[0x13] == 0x20);
  CHECK(mem[0x14] == 0x9B);
  CHECK(mem[0x15] == 0xC9);
  CHECK(mem[0x16] == 0x4C);
  CHECK(mem[0x17] == 0xB0);
  CHECK(mem[0x18] == 0xC9);
}

// Single-instruction steps report an executed count of 0, so the floating
// bus is the scanner's byte at the frame's first cycle; a marker placed
// there and then replaced is what no constant could reproduce.
TEST_CASE(
    "Super Serial Card: a fresh card reads $10, $00, $00 at $C0n9-$C0nB, the "
    "switches at $C0n1-$C0n2, and the floating bus at every other offset") {
  SerialMachine_t machine;
  const uint16_t scanner = video_get_scanner_address(nullptr, 0);
  const std::vector<uint8_t> undecoded = {0x0, 0x3, 0x4, 0x5, 0x6,
                                          0x7, 0xC, 0xD, 0xE, 0xF};
  Program_t program;
  program.read_card_into(machine.slot, register_status, 0x10);
  program.read_card_into(machine.slot, register_control, 0x11);
  program.read_card_into(machine.slot, register_command, 0x12);
  program.read_card_into(machine.slot, register_switches_1, 0x13);
  program.read_card_into(machine.slot, register_switches_2, 0x14);
  uint8_t store = 0x20;
  for (uint8_t offset : undecoded) {
    program.read_card_into(machine.slot, offset, store++);
  }
  const uint16_t spin = program.spin();
  program.poke();

  for (uint8_t marker : {uint8_t{0x5A}, uint8_t{0xDA}}) {
    CAPTURE(marker);
    const std::array<uint8_t, 1> byte = {marker};
    TestFixtures::ScopedCore_t::poke(scanner, byte);
    machine.run_until(program_start, spin);
    CHECK(mem[0x10] == 0x10);
    CHECK(mem[0x11] == 0x00);
    CHECK(mem[0x12] == 0x00);
    CHECK(mem[0x13] == 0xEC);
    CHECK(mem[0x14] == 0x52);
    for (size_t i = 0; i < undecoded.size(); ++i) {
      CAPTURE(undecoded.at(i));
      CHECK(mem[0x20 + i] == marker);
    }
  }
  CHECK(machine.sink.bytes().empty());
}

TEST_CASE(
    "Super Serial Card: on the 6502 a received byte reads $98, $18, $18, the "
    "byte, then $10, the status read releasing the slot's interrupt line") {
  SerialMachine_t machine;
  machine.sink.push_rx(machine.slot, 0xC1);
  Program_t program;
  program.program_acia(machine.slot, control_9600_8n1, command_dtr_rx_irq);
  program.delay();
  program.read_card_into(machine.slot, register_status, 0x10);
  program.read_card_into(machine.slot, register_status, 0x11);
  program.read_card_into(machine.slot, register_status, 0x12);
  program.read_card_into(machine.slot, register_data, 0x13);
  program.read_card_into(machine.slot, register_status, 0x14);
  const uint16_t spin = program.spin();
  program.poke();
  machine.run_until(program_start, spin);

  CHECK(mem[0x10] == 0x98);
  CHECK(mem[0x11] == 0x18);
  CHECK(mem[0x12] == 0x18);
  CHECK(mem[0x13] == 0xC1);
  CHECK(mem[0x14] == 0x10);
  CHECK(machine.sink.reads(machine.slot) >= 1);
  CHECK(machine.sink.bytes().empty());
}

TEST_CASE(
    "Super Serial Card: with the data read first the status reads $90 and "
    "then $10, and with the receive interrupt disabled $18 and no interrupt") {
  SerialMachine_t machine;
  poke_counting_handler(machine.slot, register_status);

  machine.sink.push_rx(machine.slot, 0xC1);
  Program_t first;
  first.program_acia(machine.slot, control_9600_8n1, command_dtr_rx_irq);
  first.delay();
  first.read_card_into(machine.slot, register_data, 0x10);
  first.read_card_into(machine.slot, register_status, 0x11);
  first.read_card_into(machine.slot, register_status, 0x12);
  const uint16_t first_spin = first.spin();
  first.poke();
  machine.run_until(program_start, first_spin);
  CHECK(mem[0x10] == 0xC1);
  CHECK(mem[0x11] == 0x90);
  CHECK(mem[0x12] == 0x10);

  // Interrupts stay enabled through the delay, so a handler entry would count.
  zero_page_clear(0x06, 0x06);
  machine.sink.push_rx(machine.slot, 0xC2);
  Program_t second;
  second.program_acia(machine.slot, control_9600_8n1, command_firmware);
  second.cli();
  second.delay();
  second.read_card_into(machine.slot, register_status, 0x10);
  second.read_card_into(machine.slot, register_data, 0x11);
  const uint16_t second_spin = second.spin();
  second.poke();
  machine.run_until(program_start, second_spin);
  CHECK(mem[0x06] == 0);
  CHECK(mem[0x10] == 0x18);
  CHECK(mem[0x11] == 0xC2);
}

TEST_CASE(
    "Super Serial Card: on the 6502 TDRE is set at once after a write to an "
    "idle transmitter and 1,063 cycles after it for a byte written behind") {
  SerialMachine_t machine;
  Program_t program;
  program.program_acia(machine.slot, control_9600_8n1, command_firmware);
  program.lda_imm(0xC1);
  const uint16_t first_write = program.here();
  program.sta_card(machine.slot, register_data);
  program.read_card_into(machine.slot, register_status, 0x10);
  program.write_card(machine.slot, register_data, 0xC2);
  program.read_card_into(machine.slot, register_status, 0x11);
  // LDA $C0n9 / AND #$10 / BEQ: a 9-cycle poll that quantises the boundary.
  const uint16_t poll = program.here();
  program.lda_card(machine.slot, register_status);
  program.and_imm(0x10);
  program.beq(poll);
  const uint16_t spin = program.spin();
  program.poke();

  TestFixtures::enter_at({program_start, 0, 0, 0});
  CpuRegisters_t* regs = cpu_get_registers();
  uint64_t first_write_at = 0;
  std::vector<uint64_t> polls;
  uint32_t cycles = 0;
  while (regs->pc != spin && cycles < cycle_cap) {
    if (regs->pc == first_write) {
      first_write_at = cpu_get_cumulative_cycles();
    }
    if (regs->pc == poll) {
      polls.push_back(cpu_get_cumulative_cycles());
    }
    cycles += cpu_execute(0);
  }
  REQUIRE(regs->pc == spin);

  CHECK((mem[0x10] & 0x10) == 0x10);
  CHECK((mem[0x11] & 0x10) == 0x00);
  REQUIRE(polls.size() >= 2);
  const uint64_t boundary = first_write_at + char_9600_8n1;
  CHECK(polls.back() >= boundary);
  CHECK(polls.back() < boundary + 9);
  CHECK(polls.at(polls.size() - 2) < boundary);

  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{0xC1, 0xC2}));
  CHECK(machine.sink.line_sets() >= 1);
  CHECK(machine.sink.last_line().baud == 9600);
  CHECK(machine.sink.last_line().data_bits == 8);
  CHECK(machine.sink.last_line().parity == peripheral_serial_parity_none);
  CHECK(machine.sink.last_line().stop_half_bits == 2);
  CHECK(machine.sink.last_line().dtr == 1);
  CHECK(machine.sink.last_line().rts == 1);
  CHECK(machine.sink.last_line().brk == 0);
}

TEST_CASE(
    "Super Serial Card: three writes four cycles apart deliver the first and "
    "third bytes, the second having been overwritten in the TDR") {
  SerialMachine_t machine;
  Program_t program;
  program.program_acia(machine.slot, control_9600_8n1, command_firmware);
  program.lda_imm(0xC1);
  program.ldx_imm(0xC2);
  program.ldy_imm(0xC3);
  program.sta_card(machine.slot, register_data);
  program.stx_abs(card_address(machine.slot, register_data));
  program.sty_abs(card_address(machine.slot, register_data));
  program.delay();
  program.read_card_into(machine.slot, register_status, 0x10);
  const uint16_t spin = program.spin();
  program.poke();
  machine.run_until(program_start, spin);

  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{0xC1, 0xC3}));
  CHECK((mem[0x10] & 0x10) == 0x10);
}

// Command bits 3-2 at 00 switch the transmitter off (SY6551 Fig. 7).
TEST_CASE(
    "Super Serial Card: a byte written with the transmitter off, or with CTS "
    "deasserted, waits in the TDR and goes out when the transmitter runs") {
  SerialMachine_t machine;
  Program_t off;
  off.program_acia(machine.slot, control_9600_8n1, command_dtr_only);
  off.write_card(machine.slot, register_data, 0xC1);
  off.delay();
  off.read_card_into(machine.slot, register_status, 0x10);
  off.write_card(machine.slot, register_command, command_firmware);
  off.read_card_into(machine.slot, register_status, 0x11);
  const uint16_t off_spin = off.spin();
  off.poke();
  machine.run_until(program_start, off_spin);
  CHECK((mem[0x10] & 0x10) == 0x00);
  CHECK((mem[0x11] & 0x10) == 0x10);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{0xC1}));

  machine.sink.set_lines(TestFixtures::ScopedByteSink_t::all_lines_asserted &
                         ~uint8_t{0x01});
  Program_t parked;
  parked.delay();
  parked.write_card(machine.slot, register_data, 0xC2);
  parked.delay();
  parked.read_card_into(machine.slot, register_status, 0x12);
  const uint16_t parked_spin = parked.spin();
  parked.poke();
  machine.run_until(program_start, parked_spin);
  CHECK((mem[0x12] & 0x10) == 0x00);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{0xC1}));

  machine.sink.set_lines(TestFixtures::ScopedByteSink_t::all_lines_asserted);
  Program_t released;
  released.read_card_into(machine.slot, register_status, 0x13);
  const uint16_t released_spin = released.spin();
  released.poke();
  machine.run_until(program_start, released_spin);
  CHECK((mem[0x13] & 0x10) == 0x10);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{0xC1, 0xC2}));
}

// The wire never produces an overrun, so OVRN is injected through the frame.
TEST_CASE(
    "Super Serial Card: a programmed reset keeps the control register, "
    "clears the command register and the overrun bit, drops DTR and leaves a "
    "receive interrupt asserted; a hardware reset leaves the RDR") {
  SerialMachine_t machine;
  Frame_t overrun = frame_header();
  overrun.at(frame_control) = control_9600_8n1;
  overrun.at(frame_command) = command_dtr_rx_irq;
  overrun.at(frame_latches) = latch_overrun | latch_rdrf;
  overrun.at(frame_receive_data) = 0xC1;
  load_frame(machine.slot, overrun);
  CHECK(machine.sink.last_line().dtr == 1);

  Program_t program;
  program.read_card_into(machine.slot, register_status, 0x10);
  program.sta_card(machine.slot, register_status);
  program.read_card_into(machine.slot, register_status, 0x11);
  program.read_card_into(machine.slot, register_control, 0x12);
  program.read_card_into(machine.slot, register_command, 0x13);
  program.read_card_into(machine.slot, register_data, 0x14);
  const uint16_t spin = program.spin();
  program.poke();
  machine.run_until(program_start, spin);
  CHECK(mem[0x10] == 0x1C);
  CHECK(mem[0x11] == 0x18);
  CHECK(mem[0x12] == control_9600_8n1);
  CHECK(mem[0x13] == 0x00);
  CHECK(mem[0x14] == 0xC1);
  CHECK(machine.sink.last_line().dtr == 0);
  CHECK(machine.sink.last_line().rts == 0);
  CHECK(machine.sink.last_line().baud == 9600);

  overrun.at(frame_irq) = 1;
  load_frame(machine.slot, overrun);
  CHECK(save_frame(machine.slot).at(frame_irq) == 1);
  Program_t reset_only;
  reset_only.sta_card(machine.slot, register_status);
  const uint16_t reset_spin = reset_only.spin();
  reset_only.poke();
  machine.run_until(program_start, reset_spin);
  CHECK(save_frame(machine.slot).at(frame_irq) == 1);
  CHECK(save_frame(machine.slot).at(frame_latches) == latch_rdrf);
  read_registers(machine);
  CHECK(mem[0x10] == 0x98);
  CHECK(mem[0x12] == 0x00);

  linapple_reset_hard();
  read_registers(machine);
  CHECK(mem[0x10] == 0x10);
  CHECK(mem[0x11] == 0x00);
  CHECK(mem[0x12] == 0x00);
  CHECK(mem[0x13] == 0xC1);
}

namespace {

constexpr SuperSerialSwitches_t printer_mode_switches = {0x68, 0x0B};

// Into $10 and $11, after a write to each register the card must ignore.
auto read_switches(SerialMachine_t& machine) -> void {
  Program_t program;
  program.lda_imm(0xFF);
  program.sta_card(machine.slot, register_switches_1);
  program.sta_card(machine.slot, register_switches_2);
  program.read_card_into(machine.slot, register_switches_1, 0x10);
  program.read_card_into(machine.slot, register_switches_2, 0x11);
  const uint16_t spin = program.spin();
  program.poke();
  machine.run_until(program_start, spin);
}

// The queue drains in peripheral_manager_think and nowhere else.
auto set_switches(SerialMachine_t& machine,
                  const SuperSerialSwitches_t& switches) -> void {
  REQUIRE(peripheral_command(machine.slot, SUPER_SERIAL_CMD_SET_SWITCHES,
                             &switches, sizeof(switches)) == peripheral_ok);
  peripheral_manager_think(0);
}

}  // namespace

TEST_CASE(
    "Super Serial Card: $C0n1 and $C0n2 read the switch blocks with the "
    "undriven bits set and CTS in bit 0, $EC and $52 under the default") {
  SerialMachine_t machine;
  read_switches(machine);
  CHECK(mem[0x10] == 0xEC);
  CHECK(mem[0x11] == 0x52);

  set_switches(machine, printer_mode_switches);
  read_switches(machine);
  CHECK(mem[0x10] == 0xEE);
  CHECK(mem[0x11] == 0x5A);

  machine.sink.set_lines(TestFixtures::ScopedByteSink_t::all_lines_asserted &
                         ~uint8_t{0x01});
  read_switches(machine);
  CHECK(mem[0x10] == 0xEE);
  CHECK(mem[0x11] == 0x5B);

  set_switches(machine, {0x78, 0x2F});
  read_switches(machine);
  CHECK(mem[0x11] == 0x53);
  machine.sink.set_lines(TestFixtures::ScopedByteSink_t::all_lines_asserted);
}

TEST_CASE("Super Serial Card: the switch image survives a hardware reset") {
  SerialMachine_t machine;
  set_switches(machine, printer_mode_switches);
  linapple_reset_hard();
  read_switches(machine);
  CHECK(mem[0x10] == 0xEE);
  CHECK(mem[0x11] == 0x5A);
}

// A break begins at the next character boundary and lasts while command
// bits 3-2 stay at 11 (W65C51S p. 21).
TEST_CASE(
    "Super Serial Card: command $0F reports a break to the host and parks a "
    "written byte until the break ends, and $00 in both registers reports no "
    "clock") {
  SerialMachine_t machine;
  Program_t brk;
  brk.program_acia(machine.slot, control_9600_8n1, command_break);
  brk.write_card(machine.slot, register_data, 0xC1);
  brk.delay();
  brk.lda_card(machine.slot, register_switches_1);
  const uint16_t brk_spin = brk.spin();
  brk.poke();
  machine.run_until(program_start, brk_spin);
  CHECK(machine.sink.last_line().brk == 1);
  CHECK(machine.sink.last_line().rts == 1);
  CHECK(machine.sink.bytes().empty());

  Program_t release;
  release.write_card(machine.slot, register_command, command_firmware);
  const uint16_t release_spin = release.spin();
  release.poke();
  machine.run_until(program_start, release_spin);
  CHECK(machine.sink.last_line().brk == 0);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{0xC1}));

  Program_t no_clock;
  no_clock.program_acia(machine.slot, 0x00, 0x00);
  const uint16_t no_clock_spin = no_clock.spin();
  no_clock.poke();
  machine.run_until(program_start, no_clock_spin);
  CHECK(machine.sink.last_line().baud == 0);
  CHECK(machine.sink.last_line().dtr == 0);
  CHECK(machine.sink.last_line().rts == 0);
}

TEST_CASE(
    "Super Serial Card: with three bytes queued the line is read once until "
    "the first character completes and the RDR is read") {
  SerialMachine_t machine;
  for (uint8_t byte : {uint8_t{0xC1}, uint8_t{0xC2}, uint8_t{0xC3}}) {
    machine.sink.push_rx(machine.slot, byte);
  }
  Program_t program;
  program.program_acia(machine.slot, control_9600_8n1, command_firmware);
  const uint16_t enabled = program.spin();
  const uint16_t after_delay_entry = program.here();
  program.delay();
  program.lda_card(machine.slot, register_switches_1);
  const uint16_t completed = program.spin();
  const uint16_t read_entry = program.here();
  program.read_card_into(machine.slot, register_data, 0x10);
  const uint16_t emptied = program.spin();
  program.poke();

  machine.run_until(program_start, enabled);
  CHECK(machine.sink.reads(machine.slot) == 1);
  machine.run_until(after_delay_entry, completed);
  CHECK(machine.sink.reads(machine.slot) == 1);
  machine.run_until(read_entry, emptied);
  CHECK(mem[0x10] == 0xC1);
  CHECK(machine.sink.reads(machine.slot) == 2);
}

namespace {

// The spin reads $C0n1, which advances the chip without touching the ACIA.
auto poke_receive_loop(int slot, uint8_t command, uint16_t* command_write_at)
    -> uint16_t {
  Program_t program;
  program.write_card(slot, register_control, control_9600_8n1);
  program.lda_imm(command);
  *command_write_at = program.here();
  program.sta_card(slot, register_command);
  program.cli();
  const uint16_t loop = program.here();
  program.lda_card(slot, register_switches_1);
  program.jmp(loop);
  program.poke();
  return loop;
}

struct Entry_t {
  uint64_t at;
  uint8_t count;
};

auto step_recording_entries(uint16_t entry, uint32_t cycles, uint16_t watched,
                            uint64_t* watched_at) -> std::vector<Entry_t> {
  std::vector<Entry_t> entries;
  TestFixtures::enter_at({entry, 0, 0, 0});
  const CpuRegisters_t* regs = cpu_get_registers();
  uint32_t ran = 0;
  while (ran < cycles) {
    if (regs->pc == watched && watched_at != nullptr) {
      *watched_at = cpu_get_cumulative_cycles();
    }
    if (regs->pc == handler_start) {
      entries.push_back({cpu_get_cumulative_cycles(), mem[0x06]});
    }
    ran += cpu_execute(0);
  }
  return entries;
}

}  // namespace

// RDRF at the command write plus 1,016, noticed by a read of the 7-cycle
// loop within six cycles, the read's four cycles, then the seven of the
// interrupt entry.
TEST_CASE(
    "Super Serial Card: a received byte vectors the 6502 through $FFFE, the "
    "handler's status read releases the line and a data read does not") {
  uint16_t command_write = 0;
  {
    SerialMachine_t machine;
    zero_page_clear(0x06, 0x06);
    poke_counting_handler(machine.slot, register_status);
    machine.sink.push_rx(machine.slot, 0xC1);
    poke_receive_loop(machine.slot, command_dtr_rx_irq, &command_write);
    uint64_t command_write_at = 0;
    const std::vector<Entry_t> entries = step_recording_entries(
        program_start, 3000, command_write, &command_write_at);
    REQUIRE(entries.size() == 1);
    CHECK(mem[0x06] == 1);
    const uint64_t rdrf_at = command_write_at + rdrf_9600_8n1;
    CHECK(entries.at(0).at >= rdrf_at + 4 + 7);
    CHECK(entries.at(0).at <= rdrf_at + 6 + 4 + 7);
  }

  // A data read clears RDRF but not the IRQ latch, so the line stays
  // asserted and the handler re-enters after every instruction.
  SerialMachine_t re_machine;
  zero_page_clear(0x06, 0x06);
  poke_counting_handler(re_machine.slot, register_data);
  re_machine.sink.push_rx(re_machine.slot, 0xC2);
  poke_receive_loop(re_machine.slot, command_dtr_rx_irq, &command_write);
  const std::vector<Entry_t> re_entries =
      step_recording_entries(program_start, 3000, command_write, nullptr);
  CHECK(re_entries.size() > 20);
  CHECK(mem[0x06] == re_entries.size());
  for (size_t i = 1; i < re_entries.size(); ++i) {
    CHECK(re_entries.at(i).at - re_entries.at(i - 1).at <= 30);
  }
}

// With TIC 01 the interrupt is raised at the start bit and, while the TDR
// stays empty, at every character boundary of the clock that write anchored
// (W65C51S pp. 16-17).
TEST_CASE(
    "Super Serial Card: the transmit interrupt vectors at the write that "
    "empties the TDR, again one character time later while it stays empty, "
    "and is released by a status read") {
  SerialMachine_t machine;
  zero_page_clear(0x06, 0x06);
  poke_counting_handler(machine.slot, register_status);
  Program_t program;
  program.program_acia(machine.slot, control_9600_8n1, command_dtr_tx_irq);
  program.cli();
  program.lda_imm(0xC1);
  const uint16_t write = program.here();
  program.sta_card(machine.slot, register_data);
  const uint16_t loop = program.here();
  program.lda_card(machine.slot, register_switches_1);
  program.jmp(loop);
  program.poke();

  uint64_t write_at = 0;
  const std::vector<Entry_t> entries = step_recording_entries(
      program_start, 2 * static_cast<uint32_t>(char_9600_8n1) + 400, write,
      &write_at);
  REQUIRE(entries.size() == 3);
  CHECK(mem[0x06] == 3);
  CHECK(entries.at(0).at >= write_at + 4);
  CHECK(entries.at(0).at <= write_at + 4 + 7 + 7);
  for (size_t i = 1; i < entries.size(); ++i) {
    const uint64_t expected = write_at + i * char_9600_8n1;
    CHECK(entries.at(i).at >= expected + 4);
    CHECK(entries.at(i).at <= expected + 6 + 4 + 7);
  }
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{0xC1}));
}

namespace {

// Each card advances only at its own accesses, so the loop reads both.
auto poke_two_card_programs(uint8_t handler_reads_slots) -> void {
  Program_t program;
  for (int slot : {1, 2}) {
    program.program_acia(slot, control_9600_8n1, command_dtr_rx_irq);
  }
  program.cli();
  const uint16_t loop = program.here();
  program.lda_card(1, register_switches_1);
  program.lda_card(2, register_switches_1);
  program.jmp(loop);
  program.poke();

  Program_t handler(handler_start);
  handler.inc_zp(0x06);
  if (handler_reads_slots == 1) {
    handler.lda_card(1, register_status);
  } else {
    handler.read_card_into(1, register_status, 0x10);
    handler.read_card_into(2, register_status, 0x11);
    handler.read_card_into(1, register_data, 0x12);
    handler.read_card_into(2, register_data, 0x13);
  }
  handler.rti();
  handler.poke();
  poke_irq_vector(handler_start);
}

}  // namespace

TEST_CASE(
    "Super Serial Card: two cards in slots 1 and 2 raise and release their "
    "own interrupt bits, and a byte for one never reaches the other") {
  SerialMachine_t machine({1, 2});

  zero_page_clear(0x06, 0x13);
  poke_two_card_programs(2);
  machine.sink.push_rx(1, 0xC1);
  machine.run_cycles(program_start, 3000);
  CHECK(mem[0x06] == 1);
  CHECK(mem[0x10] == 0x98);
  CHECK(mem[0x11] == 0x10);
  CHECK(mem[0x12] == 0xC1);
  CHECK(mem[0x13] == 0x00);

  zero_page_clear(0x06, 0x13);
  machine.sink.push_rx(2, 0xC2);
  machine.run_cycles(program_start, 3000);
  CHECK(mem[0x06] == 1);
  CHECK(mem[0x10] == 0x10);
  CHECK(mem[0x11] == 0x98);
  CHECK(mem[0x12] == 0xC1);
  CHECK(mem[0x13] == 0xC2);

  // Slot 2 is never acknowledged, so its bit holds the line and the handler
  // re-enters after every instruction.
  zero_page_clear(0x06, 0x13);
  poke_two_card_programs(1);
  machine.sink.push_rx(1, 0xC3);
  machine.sink.push_rx(2, 0xC4);
  machine.run_cycles(program_start, 3000);
  CHECK(mem[0x06] > 10);
}

// SW2-6 OFF keeps the ACIA's interrupt off the slot's line (1981 manual
// p. 47).
TEST_CASE(
    "Super Serial Card: with SW2-6 OFF a received byte sets status bit 7 and "
    "never reaches the slot's interrupt line") {
  SerialMachine_t machine;
  set_switches(machine, {0x78, 0x0F});
  zero_page_clear(0x06, 0x06);
  // The handler acknowledges nothing, so an asserted line would re-enter it
  // after every instruction; a count of zero proves the line stayed clear.
  Program_t handler(handler_start);
  handler.inc_zp(0x06);
  handler.rti();
  handler.poke();
  poke_irq_vector(handler_start);
  machine.sink.push_rx(machine.slot, 0xC1);
  uint16_t command_write = 0;
  poke_receive_loop(machine.slot, command_dtr_rx_irq, &command_write);
  machine.run_cycles(program_start, 3000);
  CHECK(mem[0x06] == 0);

  // $98 shows the byte arrived and the ACIA's own latch is set.
  Program_t confirm;
  confirm.read_card_into(machine.slot, register_status, 0x10);
  const uint16_t spin = confirm.spin();
  confirm.poke();
  machine.run_until(program_start, spin);
  CHECK(mem[0x10] == 0x98);
}

TEST_CASE(
    "Super Serial Card: shutting the card down with its interrupt asserted "
    "releases the slot's line") {
  SerialMachine_t machine;
  machine.sink.push_rx(machine.slot, 0xC1);
  touch_card(machine, false);
  Program_t program;
  program.program_acia(machine.slot, control_9600_8n1, command_dtr_rx_irq);
  program.delay();
  program.lda_card(machine.slot, register_switches_1);
  const uint16_t spin = program.spin();
  program.poke();
  machine.run_until(program_start, spin);
  CHECK(save_frame(machine.slot).at(frame_irq) == 1);

  REQUIRE(peripheral_unregister(machine.slot) == 0);

  // The handler acknowledges nothing, so a line still asserted would re-enter
  // it after every instruction.
  zero_page_clear(0x06, 0x06);
  Program_t handler(handler_start);
  handler.inc_zp(0x06);
  handler.rti();
  handler.poke();
  poke_irq_vector(handler_start);
  Program_t loop;
  loop.cli();
  loop.spin();
  loop.poke();
  machine.run_cycles(program_start, 500);
  CHECK(mem[0x06] == 0);
}

TEST_CASE(
    "Super Serial Card: the frame carries the registers, the latch byte and "
    "the data bytes, comes back through the ABI with the interrupt, and a "
    "byte in the receive shifter completes one character time after a load") {
  SerialMachine_t machine;
  machine.sink.push_rx(machine.slot, 0xC1);
  Program_t program;
  program.program_acia(machine.slot, control_9600_8n1, command_dtr_rx_irq);
  program.delay();
  const uint16_t spin = program.spin();
  program.poke();
  machine.run_until(program_start, spin);

  // Nothing has touched the card since the byte was pulled, so it is still
  // in the shifter.
  const Frame_t in_flight = save_frame(machine.slot);
  CHECK(in_flight.at(frame_control) == control_9600_8n1);
  CHECK(in_flight.at(frame_command) == command_dtr_rx_irq);
  CHECK(in_flight.at(frame_irq) == 0x00);
  CHECK(in_flight.at(frame_latches) == latch_rx_busy);
  CHECK(in_flight.at(frame_receive_data) == 0x00);
  CHECK(in_flight.at(frame_shift_data) == 0xC1);

  touch_card(machine, false);
  Frame_t expected = frame_header();
  expected.at(frame_control) = control_9600_8n1;
  expected.at(frame_command) = command_dtr_rx_irq;
  expected.at(frame_irq) = 0x01;
  expected.at(frame_latches) = latch_rdrf;
  expected.at(frame_receive_data) = 0xC1;
  const Frame_t received = save_frame(machine.slot);
  CHECK(hex(received) == hex(expected));

  linapple_reset_hard();
  read_registers(machine);
  CHECK(mem[0x10] == 0x10);
  CHECK(mem[0x11] == 0x00);
  CHECK(mem[0x12] == 0x00);

  load_frame(machine.slot, received);
  read_registers(machine);
  CHECK(mem[0x10] == 0x98);
  CHECK(mem[0x11] == control_9600_8n1);
  CHECK(mem[0x12] == command_dtr_rx_irq);
  CHECK(mem[0x13] == 0xC1);

  linapple_reset_hard();
  load_frame(machine.slot, in_flight);
  read_registers(machine);
  CHECK(mem[0x10] == 0x10);
  touch_card(machine, true);
  read_registers(machine);
  CHECK(mem[0x10] == 0x98);
  CHECK(mem[0x13] == 0xC1);
}

// The frame carries no cycles, so the character restarts from the load
// however the loading core's counter compares with the saving core's.
TEST_CASE(
    "Super Serial Card: a frame with a character in flight and a byte parked "
    "in the TDR restarts from the load, and the parked byte goes out within "
    "one character time of it") {
  Frame_t expected = frame_header();
  expected.at(frame_control) = control_9600_8n1;
  expected.at(frame_command) = command_firmware;
  expected.at(frame_latches) = latch_tdr_full | latch_tx_busy;
  expected.at(frame_transmit_data) = 0xC8;
  {
    SerialMachine_t saving;
    g_cumulative_cycles = 5000000;
    Program_t program;
    program.program_acia(saving.slot, control_9600_8n1, command_firmware);
    program.write_card(saving.slot, register_data, 0xC1);
    program.write_card(saving.slot, register_data, 0xC8);
    const uint16_t spin = program.spin();
    program.poke();
    saving.run_until(program_start, spin);
    CHECK(hex(save_frame(saving.slot)) == hex(expected));
    CHECK(hex(saving.stream()) == hex(std::vector<uint8_t>{0xC1}));
  }

  SerialMachine_t loading;
  CHECK(cpu_get_cumulative_cycles() < 5000000);
  load_frame(loading.slot, expected);
  Program_t program;
  program.read_card_into(loading.slot, register_status, 0x10);
  program.delay();
  program.read_card_into(loading.slot, register_status, 0x11);
  const uint16_t spin = program.spin();
  program.poke();
  loading.run_until(program_start, spin);
  CHECK((mem[0x10] & 0x10) == 0x00);
  CHECK((mem[0x11] & 0x10) == 0x10);
  CHECK(hex(loading.stream()) == hex(std::vector<uint8_t>{0xC8}));
}

TEST_CASE(
    "Super Serial Card: a legacy frame with a queue, flags and a "
    "configuration loads as an idle card at its registers") {
  SerialMachine_t machine;
  Frame_t legacy = frame_header();
  legacy.at(frame_rx_count) = 0x03;
  legacy.at(frame_control) = control_9600_8n1;
  legacy.at(frame_command) = command_firmware;
  legacy.at(15) = 0x01;
  legacy.at(16) = 0x01;
  legacy.at(17) = 0x01;
  for (size_t i = 18; i < 27; ++i) {
    legacy.at(i) = static_cast<uint8_t>(0xA0 + i);
  }
  for (size_t i = 28; i < 52; ++i) {
    legacy.at(i) = 0x5A;
  }
  load_frame(machine.slot, legacy);
  read_registers(machine);
  CHECK(mem[0x10] == 0x10);
  CHECK(mem[0x11] == control_9600_8n1);
  CHECK(mem[0x12] == command_firmware);
  CHECK(machine.sink.last_line().baud == 9600);
  CHECK(machine.sink.last_line().dtr == 1);
}

// An older .aws may hold the IRQ byte set with bytes queued.
TEST_CASE(
    "Super Serial Card: a legacy frame with the interrupt byte set and a "
    "queue loads as an interrupt with nothing to read, $90 then $10") {
  SerialMachine_t machine;
  Frame_t legacy = frame_header();
  legacy.at(frame_rx_count) = 0x02;
  legacy.at(frame_control) = control_9600_8n1;
  legacy.at(frame_command) = command_dtr_rx_irq;
  legacy.at(frame_irq) = 0x01;
  legacy.at(18) = 0xC1;
  legacy.at(19) = 0xC2;
  load_frame(machine.slot, legacy);
  Program_t program;
  program.read_card_into(machine.slot, register_status, 0x10);
  program.read_card_into(machine.slot, register_status, 0x11);
  const uint16_t spin = program.spin();
  program.poke();
  machine.run_until(program_start, spin);
  CHECK(mem[0x10] == 0x90);
  CHECK(mem[0x11] == 0x10);
}

TEST_CASE(
    "Super Serial Card: load_state refuses the wrong version, a wrong size, "
    "a short buffer and a set bit 7 of the latch byte, changing nothing") {
  SerialMachine_t machine;
  Frame_t good = frame_header();
  good.at(frame_control) = control_9600_8n1;
  good.at(frame_command) = command_firmware;
  load_frame(machine.slot, good);

  Frame_t bad = good;
  bad.at(frame_control) = 0x16;
  bad.at(0) = 0x02;
  CHECK(peripheral_load_state(machine.slot, bad.data(), bad.size()) ==
        peripheral_error);
  bad = good;
  bad.at(frame_control) = 0x16;
  bad.at(4) = 0x37;
  CHECK(peripheral_load_state(machine.slot, bad.data(), bad.size()) ==
        peripheral_error);
  bad = good;
  bad.at(frame_control) = 0x16;
  CHECK(peripheral_load_state(machine.slot, bad.data(), bad.size() - 1) ==
        peripheral_error);
  bad = good;
  bad.at(frame_control) = 0x16;
  bad.at(frame_latches) = 0x80;
  CHECK(peripheral_load_state(machine.slot, bad.data(), bad.size()) ==
        peripheral_error);

  read_registers(machine);
  CHECK(mem[0x11] == control_9600_8n1);
  CHECK(mem[0x12] == command_firmware);

  std::array<uint8_t, frame_size + 8> larger{};
  std::copy(good.begin(), good.end(), larger.begin());
  larger.at(frame_control) = 0x16;
  CHECK(peripheral_load_state(machine.slot, larger.data(), larger.size()) ==
        peripheral_ok);
  read_registers(machine);
  CHECK(mem[0x11] == 0x16);
}

// Pascal 1.1 entry offsets sit at $Cn0D init, $Cn0F write and $Cn10 status
// (1981 manual p. 57); every entry is called with X = $Cn, Y = $n0 and
// returns an error code in X. The status entry answers A = 0 "ready to
// write" and A = 1 "a byte is ready" with carry set for yes: its bytes at
// $Cn9A test TDRE with DSR and DCD asserted (JSR $CAF5: AND #$70 / CMP #$10)
// and RDRF (JSR $CAD2). Table A-11 of the manual states the opposite and is
// in error.
TEST_CASE(
    "Super Serial Card: the Pascal 1.1 init, write and status entries run "
    "from the slot page without touching $CFFF") {
  SerialMachine_t machine;
  const auto slot_page_hi = static_cast<uint8_t>(0xC0 + machine.slot);
  const auto slot_page = static_cast<uint16_t>(slot_page_hi << 8);
  const auto entry_at = [slot_page](uint8_t offset) -> uint16_t {
    return static_cast<uint16_t>(slot_page + mem[slot_page + offset]);
  };
  const uint16_t init_entry = entry_at(0x0D);
  const uint16_t write_entry = entry_at(0x0F);
  const uint16_t status_entry = entry_at(0x10);
  CHECK(init_entry == slot_page + 0x8E);
  CHECK(write_entry == slot_page + 0x97);
  CHECK(status_entry == slot_page + 0x9A);

  const auto slot_y = static_cast<uint8_t>(machine.slot << 4);
  const auto call = [&](Program_t& p, uint16_t entry) {
    p.ldx_imm(slot_page_hi);
    p.ldy_imm(slot_y);
    p.jsr(entry);
  };
  const auto status_into = [&](Program_t& p, uint8_t request, uint8_t store) {
    p.ldx_imm(slot_page_hi);
    p.ldy_imm(slot_y);
    p.lda_imm(request);
    p.jsr(status_entry);
    p.php();
    p.pla();
    p.sta_zp(store);
  };

  Program_t program;
  call(program, init_entry);
  program.stx_zp(0x10);
  const uint16_t initialised = program.spin();
  const uint16_t write_start = program.here();
  program.lda_imm(0x41);
  program.ldx_imm(slot_page_hi);
  program.ldy_imm(slot_y);
  program.jsr(write_entry);
  program.stx_zp(0x11);
  status_into(program, 0x00, 0x12);
  program.stx_zp(0x13);
  program.delay();
  status_into(program, 0x00, 0x14);
  status_into(program, 0x01, 0x15);
  const uint16_t written = program.spin();
  const uint16_t receive_start = program.here();
  program.lda_card(machine.slot, register_switches_1);
  program.delay();
  status_into(program, 0x01, 0x16);
  const uint16_t received = program.spin();
  program.poke();

  machine.run_until(program_start, initialised);
  CHECK(mem[0x10] == 0);
  Frame_t frame = save_frame(machine.slot);
  CHECK(frame.at(frame_control) == control_9600_8n1);
  CHECK(frame.at(frame_command) == command_firmware);

  machine.run_until(write_start, written);
  CHECK(mem[0x11] == 0);
  CHECK(hex(machine.stream()) == hex(std::vector<uint8_t>{0x41}));
  CHECK((mem[0x12] & 0x01) == 1);
  CHECK(mem[0x13] == 0);
  CHECK((mem[0x14] & 0x01) == 1);
  CHECK((mem[0x15] & 0x01) == 0);

  machine.sink.push_rx(machine.slot, 0xC1);
  machine.run_until(receive_start, received);
  CHECK((mem[0x16] & 0x01) == 1);
}

// The sessions type at the Applesoft prompt, which needs the keyboard card.
#ifdef ENABLE_PERIPHERAL_KEYBOARD
namespace {

constexpr uint8_t high_cr = 0x8D;
constexpr uint8_t high_prompt = 0xDD;

// After PR#2 hooks CSW: CRDO and the prompt, GETLN's echo of PRINT "HELLO"
// and its Return, HELLO, CRDO and the prompt, GETLN's echo of PR#0 and its
// Return. In communications mode the firmware sends each COUT byte with bit 7
// intact and no line feed (SW2-5 OFF).
constexpr std::array<uint8_t, 29> applesoft_session_stream = {
    {0x8D, 0xDD, 0xD0, 0xD2, 0xC9, 0xCE, 0xD4, 0xA0, 0xA2, 0xC8,
     0xC5, 0xCC, 0xCC, 0xCF, 0xA2, 0x8D, 0xC8, 0xC5, 0xCC, 0xCC,
     0xCF, 0x8D, 0x8D, 0xDD, 0xD0, 0xD2, 0xA3, 0xB0, 0x8D}};

}  // namespace

TEST_CASE(
    "Super Serial Card: PR#2 at the Applesoft prompt programs the card to "
    "9600 8N1, hooks CSW to the slot page and streams the session to the "
    "line with bit 7 set and no line feeds") {
  SerialSession_t session;
  session.boot_to_prompt();
  REQUIRE(session.sink.bytes().empty());

  session.harness.type_string("PR#2\r", 2);
  session.harness.run_frames(4);
  Frame_t frame = save_frame(session.slot);
  CHECK(frame.at(frame_control) == control_9600_8n1);
  CHECK(frame.at(frame_command) == command_firmware);
  CHECK(mem[0x36] == 0x07);
  CHECK(mem[0x37] == 0xC0 + session.slot);
  CHECK(session.sink.last_line().baud == 9600);
  CHECK(session.sink.last_line().dtr == 1);
  const unsigned line_sets_after_first = session.sink.line_sets();

  session.harness.type_string("PRINT \"HELLO\"\r", 2);
  session.harness.run_frames(4);
  session.harness.type_string("PR#0\r", 2);
  session.harness.run_frames(4);
  CHECK(hex(session.stream()) == hex(applesoft_session_stream));
  CHECK(session.sink.dropped() == 0);
  CHECK(session.screen_has_row("HELLO"));

  // A second PR#2 finds the command register programmed and leaves both
  // registers alone, so no new line format reaches the host.
  session.harness.type_string("PR#2\r", 2);
  session.harness.run_frames(4);
  frame = save_frame(session.slot);
  CHECK(frame.at(frame_control) == control_9600_8n1);
  CHECK(frame.at(frame_command) == command_firmware);
  CHECK(session.sink.line_sets() == line_sets_after_first);
}

// The firmware ORs $80 into each received byte for GETLN, which stores it at
// $0200 with no echo to the line.
TEST_CASE(
    "Super Serial Card: IN#2 at the Applesoft prompt takes a line from the "
    "card into the input buffer and the next line is executed") {
  SerialSession_t session;
  session.boot_to_prompt();
  session.harness.type_string("IN#2\r", 2);
  session.harness.run_frames(4);
  Frame_t frame = save_frame(session.slot);
  CHECK(frame.at(frame_control) == control_9600_8n1);
  CHECK(frame.at(frame_command) == command_firmware);

  const std::array<uint8_t, 16> zeros{};
  TestFixtures::ScopedCore_t::poke(0x0200, zeros);
  for (char c : std::string("HELLO")) {
    session.sink.push_rx(session.slot, static_cast<uint8_t>(c));
  }
  session.harness.run_frames(3);
  CHECK(mem[0x0200] == 0xC8);
  CHECK(mem[0x0201] == 0xC5);
  CHECK(mem[0x0202] == 0xCC);
  CHECK(mem[0x0203] == 0xCC);
  CHECK(mem[0x0204] == 0xCF);
  CHECK(mem[0x0205] == 0x00);
  // KSW points at the slot page's input entry, $Cn05.
  CHECK(mem[0x38] == 0x05);
  CHECK(mem[0x39] == 0xC0 + session.slot);
  CHECK(session.sink.bytes().empty());

  // HELLO alone is a syntax error; both it and the 42 stay on the screen.
  session.sink.push_rx(session.slot, '\r');
  session.harness.run_frames(6);
  for (char c : std::string("PRINT 7*6\r")) {
    session.sink.push_rx(session.slot, static_cast<uint8_t>(c));
  }
  session.harness.run_frames(12);
  CHECK(session.screen_has_row("?SYNTAX ERROR"));
  CHECK(session.screen_has_row("42"));
}

// Before every byte the firmware's output path loops at $CC02 on JSR $CAF5
// until TDRE is set with DSR and DCD asserted (1981 manual p. 31: "the card
// will wait until the peripheral is ready").
TEST_CASE(
    "Super Serial Card: with DSR or DCD deasserted PR#2 waits in the "
    "firmware's ready loop and sends nothing until the line is asserted") {
  SerialSession_t session;
  session.boot_to_prompt();
  session.sink.set_lines(0x01);
  session.harness.type_string("PR#2\r", 2);
  session.harness.run_frames(10);
  Frame_t frame = save_frame(session.slot);
  CHECK(frame.at(frame_control) == control_9600_8n1);
  CHECK(frame.at(frame_command) == command_firmware);
  CHECK(session.sink.bytes().empty());

  session.sink.set_lines(TestFixtures::ScopedByteSink_t::all_lines_asserted);
  session.harness.run_frames(4);
  CHECK(hex(session.stream()) ==
        hex(std::vector<uint8_t>{high_cr, high_prompt}));
}
#endif

namespace {

constexpr uint16_t byte_table = 0x0600;
constexpr uint16_t counter_low_table = 0x0620;
constexpr uint16_t counter_high_table = 0x0640;
constexpr size_t metered_bytes = 16;

// INC $06 / BNE is 8 cycles a turn; the turn that wraps $06 runs the BNE not
// taken, INC $07 and the JMP for 15.
auto meter_cycles(uint32_t turns) -> uint64_t {
  return 8ULL * turns + 7ULL * (turns / 256);
}

// The priming read pulls the first queued byte at once rather than at the
// frame's end; the meter loop touches no card register.
auto poke_meter_program(int slot, bool prime_with_read) -> void {
  Program_t program;
  program.cli();
  if (prime_with_read) {
    program.lda_card(slot, register_switches_1);
  }
  const uint16_t loop = program.here();
  program.inc_zp(0x06);
  program.bne(loop);
  program.inc_zp(0x07);
  program.jmp(loop);
  program.poke();
}

// 7 cycles of entry, then 4+4+3+5+3+5+3+5+5+6 = 43.
constexpr uint64_t receive_handler_cycles = 50;

auto poke_receive_meter_handler(int slot) -> void {
  Program_t handler(handler_start);
  handler.lda_card(slot, register_status);
  handler.lda_card(slot, register_data);
  handler.ldx_zp(0x08);
  handler.sta_abs_x(byte_table);
  handler.lda_zp(0x06);
  handler.sta_abs_x(counter_low_table);
  handler.lda_zp(0x07);
  handler.sta_abs_x(counter_high_table);
  handler.inc_zp(0x08);
  handler.rti();
  handler.poke();
  poke_irq_vector(handler_start);
}

// 7 cycles of entry, then 4+3+3+5+3+5+5+2+2+2+2+2+4+6 = 48 on the sending
// path.
constexpr uint64_t transmit_handler_cycles = 55;

auto poke_transmit_meter_handler(int slot) -> void {
  Program_t handler(handler_start);
  handler.lda_card(slot, register_status);
  handler.ldx_zp(0x08);
  handler.lda_zp(0x06);
  handler.sta_abs_x(counter_low_table);
  handler.lda_zp(0x07);
  handler.sta_abs_x(counter_high_table);
  handler.inc_zp(0x08);
  handler.cpx_imm(metered_bytes - 1);
  const uint16_t stop_branch = handler.here();
  handler.bcs(stop_branch);  // patched below once the target is known
  handler.txa();
  handler.clc();
  handler.adc_imm(0xC2);
  handler.sta_card(slot, register_data);
  handler.rti();
  const uint16_t stop = handler.here();
  handler.write_card(slot, register_command, command_firmware);
  handler.rti();
  handler.bytes.at(stop_branch + 1 - handler_start) =
      static_cast<uint8_t>(stop - (stop_branch + 2));
  handler.poke();
  poke_irq_vector(handler_start);
}

struct Meter_t {
  std::vector<uint8_t> bytes;
  std::vector<uint64_t> cycles;
};

// The 16-bit meter wraps once in a long run.
auto read_meter(size_t count) -> Meter_t {
  Meter_t meter;
  uint32_t previous = 0;
  uint32_t carry = 0;
  for (size_t i = 0; i < count; ++i) {
    meter.bytes.push_back(mem[byte_table + i]);
    const uint32_t turns =
        mem[counter_low_table + i] | (mem[counter_high_table + i] << 8);
    if (turns < previous) {
      carry += 0x10000;
    }
    previous = turns;
    meter.cycles.push_back(meter_cycles(turns + carry));
  }
  return meter;
}

// The gap is quantised by the loop (a turn of 8 or 15) and by the instruction
// boundary at which each interrupt is taken (up to 7, plus the instruction).
auto gaps_are_one_character(const Meter_t& meter, uint64_t character,
                            uint64_t handler) -> void {
  constexpr uint64_t tolerance = 32;
  REQUIRE(meter.cycles.size() >= 2);
  for (size_t i = 1; i < meter.cycles.size(); ++i) {
    CAPTURE(i);
    const uint64_t gap = meter.cycles.at(i) - meter.cycles.at(i - 1) + handler;
    CHECK(gap + tolerance >= character);
    CHECK(gap <= character + tolerance);
  }
}

auto prepare_metered_session(SerialSession_t& session, uint8_t control,
                             uint8_t command) -> void {
  session.harness.boot();
  Program_t program;
  program.program_acia(session.slot, control, command);
  const uint16_t spin = program.spin();
  program.poke();
  TestFixtures::enter_at({program_start, 0, 0, 0});
  TestFixtures::step_until_pc(spin, cycle_cap);
  REQUIRE(cpu_get_registers()->pc == spin);
  zero_page_clear(0x06, 0x08);
  const std::array<uint8_t, 96> zeros{};
  TestFixtures::ScopedCore_t::poke(byte_table, zeros);
}

}  // namespace

TEST_CASE(
    "Super Serial Card: an interrupt-driven receiver takes sixteen bytes "
    "1,063 cycles apart inside one frame at 9600 baud") {
  SerialSession_t session;
  prepare_metered_session(session, control_9600_8n1, command_dtr_rx_irq);
  poke_receive_meter_handler(session.slot);
  poke_meter_program(session.slot, true);
  for (size_t i = 0; i < metered_bytes; ++i) {
    session.sink.push_rx(session.slot, static_cast<uint8_t>(0xC1 + i));
  }
  TestFixtures::enter_at({program_start, 0, 0, 0});
  session.harness.run_frames(2);

  REQUIRE(mem[0x08] == metered_bytes);
  const Meter_t meter = read_meter(metered_bytes);
  for (size_t i = 0; i < metered_bytes; ++i) {
    CHECK(meter.bytes.at(i) == 0xC1 + i);
  }
  gaps_are_one_character(meter, char_9600_8n1, receive_handler_cycles);
  CHECK(meter.cycles.back() - meter.cycles.front() < 17030);
}

TEST_CASE(
    "Super Serial Card: a receiver that never touches the card gets its first "
    "byte at the frame's end and the rest at the programmed rate") {
  SerialSession_t session;
  prepare_metered_session(session, control_9600_8n1, command_dtr_rx_irq);
  poke_receive_meter_handler(session.slot);
  poke_meter_program(session.slot, false);
  for (size_t i = 0; i < metered_bytes; ++i) {
    session.sink.push_rx(session.slot, static_cast<uint8_t>(0xC1 + i));
  }
  TestFixtures::enter_at({program_start, 0, 0, 0});
  session.harness.run_frames(1);
  CHECK(mem[0x08] == 0);
  session.harness.run_frames(2);
  REQUIRE(mem[0x08] == metered_bytes);
  const Meter_t meter = read_meter(metered_bytes);
  CHECK(meter.cycles.front() >= 17030 - 100);
  gaps_are_one_character(meter, char_9600_8n1, receive_handler_cycles);
}

TEST_CASE(
    "Super Serial Card: an interrupt-driven sender puts sixteen bytes on the "
    "line 1,063 cycles apart inside one frame at 9600 baud") {
  SerialSession_t session;
  prepare_metered_session(session, control_9600_8n1, command_dtr_only);
  poke_transmit_meter_handler(session.slot);
  // CLI after the first write, so the first interrupt taken is the one the
  // write raises, never a re-fire of the idle clock.
  Program_t program;
  program.write_card(session.slot, register_command, command_dtr_tx_irq);
  program.write_card(session.slot, register_data, 0xC1);
  program.cli();
  const uint16_t loop = program.here();
  program.inc_zp(0x06);
  program.bne(loop);
  program.inc_zp(0x07);
  program.jmp(loop);
  program.poke();
  TestFixtures::enter_at({program_start, 0, 0, 0});
  session.harness.run_frames(2);

  REQUIRE(mem[0x08] == metered_bytes);
  std::vector<uint8_t> expected;
  expected.reserve(metered_bytes);
  for (size_t i = 0; i < metered_bytes; ++i) {
    expected.push_back(static_cast<uint8_t>(0xC1 + i));
  }
  CHECK(hex(session.stream()) == hex(expected));
  const Meter_t meter = read_meter(metered_bytes);
  gaps_are_one_character(meter, char_9600_8n1, transmit_handler_cycles);
  CHECK(meter.cycles.back() - meter.cycles.front() < 17030);
  CHECK(save_frame(session.slot).at(frame_command) == command_firmware);
}

// 34,016 cycles is two frames less 44, so every wake crosses a frame
// boundary.
TEST_CASE(
    "Super Serial Card: at 300 baud the receive interrupts come 34,016 cycles "
    "apart across frame boundaries") {
  SerialSession_t session;
  prepare_metered_session(session, control_300_8n1, command_dtr_rx_irq);
  poke_receive_meter_handler(session.slot);
  poke_meter_program(session.slot, true);
  for (size_t i = 0; i < metered_bytes; ++i) {
    session.sink.push_rx(session.slot, static_cast<uint8_t>(0xC1 + i));
  }
  TestFixtures::enter_at({program_start, 0, 0, 0});
  session.harness.run_frames(34);

  REQUIRE(mem[0x08] == metered_bytes);
  const Meter_t meter = read_meter(metered_bytes);
  gaps_are_one_character(meter, char_300_8n1, receive_handler_cycles);
}
