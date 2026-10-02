// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

extern "C" unsigned superserial_abi_c_frame_size(void);
extern "C" unsigned superserial_abi_c_state_version(void);
extern "C" unsigned superserial_abi_c_switches_size(void);
extern "C" uint32_t superserial_abi_c_set_switches_id(void);

namespace {

// The card is reached the way the emulator reaches it, through the registry,
// so one test binary covers the built-in card and a loaded plugin alike.
auto serial_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.ssc");
}

constexpr int test_slot = 2;
constexpr size_t frame_size = 56;

// A host with every member the card may ask for, each a bench stand-in: the
// card is driven through its descriptor alone here, so nothing it registers
// reaches a machine.
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
  }

  auto host() -> HostInterface_t* { return &host_; }

 private:
  static auto bench_log(void* instance, PeripheralLogLevel_t level,
                        const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    (void)fmt;
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
    (void)read_c0;
    (void)write_c0;
    (void)read_cx;
    (void)write_cx;
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
  static auto bench_sink_read(void* sink, uint8_t* byte) -> bool {
    (void)sink;
    (void)byte;
    return false;
  }
  static auto bench_sink_set_line(void* sink,
                                  const PeripheralSerialLine_t* line) -> void {
    (void)sink;
    (void)line;
  }
  static auto bench_sink_get_lines(void* sink, uint8_t* lines) -> bool {
    (void)sink;
    (void)lines;
    return false;
  }
  static auto bench_schedule_event(void* instance, uint64_t at_cycle) -> void {
    (void)instance;
    (void)at_cycle;
  }

  static int s_token;
  HostInterface_t host_{};
};

int BenchHost_t::s_token = 0;

// One card instance on the bench host, shut down with the scope.
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

// Every member the card requires, nulled one at a time: the card names the
// first missing one and builds nothing.
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
    "Super Serial Card: init refuses a host lacking any member it needs, a "
    "mute host, and a slot outside 1 to 7") {
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
  }

  HostInterface_t mute = *bench.host();
  mute.Log = nullptr;
  CHECK(card->init(test_slot, &mute) == nullptr);

  CHECK(card->init(0, bench.host()) == nullptr);
  CHECK(card->init(8, bench.host()) == nullptr);
  void* instance = card->init(7, bench.host());
  REQUIRE(instance != nullptr);
  card->shutdown(instance);
}

TEST_CASE(
    "Super Serial Card: the switch command takes exactly two bytes with bit 7 "
    "clear, and no query is answered") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  Peripheral_t* card = serial_descriptor();
  REQUIRE(card != nullptr);
  BenchCard_t bench;
  REQUIRE(bench.instance() != nullptr);

  CHECK(superserial_abi_c_switches_size() == sizeof(SuperSerialSwitches_t));
  CHECK(superserial_abi_c_set_switches_id() == SUPER_SERIAL_CMD_SET_SWITCHES);

  SuperSerialSwitches_t switches{0x68, 0x0B};
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES,
                      &switches, sizeof(switches)) == peripheral_ok);
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES,
                      &switches, 1) == peripheral_error);
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES,
                      &switches, 3) == peripheral_error);
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES, nullptr,
                      sizeof(switches)) == peripheral_error);
  switches.sw1 = 0x80;
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES,
                      &switches, sizeof(switches)) == peripheral_error);
  switches = {0x00, 0x80};
  CHECK(card->command(bench.instance(), SUPER_SERIAL_CMD_SET_SWITCHES,
                      &switches, sizeof(switches)) == peripheral_error);

  CHECK(card->command(bench.instance(), PERIPHERAL_SUBSYSTEM_SERIAL | 0x0001,
                      &switches, 1) == peripheral_incompatible);
  CHECK(card->command(bench.instance(), PERIPHERAL_SUBSYSTEM_SERIAL | 0x0002,
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
constexpr uint16_t program_start = 0x0300;
constexpr uint16_t copy_sentinel = 0x032E;
constexpr uint16_t slot_page_copy = 0x2000;
constexpr uint16_t expansion_copy = 0x2100;
constexpr size_t expansion_pages = 7;
constexpr uint32_t copy_cycle_cap = 60000;
constexpr uint8_t status_interrupts_masked = 0x24;

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

// An Enhanced //e with the card in one slot, built the way the frontend
// builds it, so the pages the 6502 sees are the ones the card registered. The
// sink is the first member so that it is installed before the core builds the
// card and still there when the card's shutdown closes it.
struct SerialMachine_t {
  static auto describe(int slot)
      -> TestFixtures::ScopedTestConfig_t::Description_t {
    TestFixtures::ScopedTestConfig_t::Description_t description;
    description.slots.at(static_cast<size_t>(slot - 1)) = "Super Serial Card";
    return description;
  }

  TestFixtures::ScopedByteSink_t sink;
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;
  int slot;

  explicit SerialMachine_t(int in_slot = test_slot)
      : config(describe(in_slot)), core(config), slot(in_slot) {
    peripheral_manager_init();
    linapple_register_peripherals();
    linapple_reset_hard();
  }

  // Steps the 6502 one instruction at a time until it reaches the sentinel
  // or spends the cap, interrupts masked, so no handler runs between steps.
  auto run_until(uint16_t entry, uint16_t sentinel, uint32_t cap) -> uint32_t {
    CpuRegisters_t* regs = cpu_get_registers();
    regs->pc = entry;
    regs->ps = status_interrupts_masked;
    uint32_t cycles = 0;
    while (regs->pc != sentinel && cycles < cap) {
      cycles += cpu_execute(0);
    }
    return cycles;
  }
};

// Copies the slot page to $2000 and, having fetched from $Cnxx, the seven
// expansion pages $C800-$CEFF to $2100-$27FF through a zero-page pointer,
// then spins. $CF00 is never touched: a read there would reset the latch.
auto poke_rom_copier(int slot) -> void {
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
}

}  // namespace

TEST_CASE(
    "Super Serial Card: the slot page is the ROM's last page, the expansion "
    "ROM is the whole image, and both match res/roms/SSC.rom byte for byte") {
  const Rom_t rom = read_rom_file();
  SerialMachine_t machine;
  poke_rom_copier(machine.slot);
  const uint32_t cycles =
      machine.run_until(program_start, copy_sentinel, copy_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == copy_sentinel);
  CHECK(cycles < copy_cycle_cap);

  // The $Cn00 entry (BIT $FF58 / BVS), the Pascal signature bytes, the
  // Pascal 1.1 init offset and the revision byte (1981 manual pp. 55-57).
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

namespace {

constexpr uint64_t char_9600_8n1 = 1063;
constexpr uint32_t cycle_cap = 20000;
constexpr uint16_t status_sequence_sentinel = 0x0328;
constexpr uint16_t tdre_sentinel = 0x0325;
constexpr uint16_t tdre_first_write = 0x030C;
constexpr uint16_t tdre_poll = 0x031E;

// Control $1E and command $09 (receiver interrupt on), a 1,279-cycle delay
// for the byte to complete, then three status reads, the data read and one
// more status read, each stored in zero page.
auto poke_status_sequence(int slot) -> void {
  const auto hi = static_cast<uint8_t>(0xC0);
  const auto status = static_cast<uint8_t>(0x89 + (slot << 4));
  const auto data = static_cast<uint8_t>(0x88 + (slot << 4));
  const auto command = static_cast<uint8_t>(0x8A + (slot << 4));
  const auto control = static_cast<uint8_t>(0x8B + (slot << 4));
  const std::array<uint8_t, 43> program = {
      0xA9, 0x1E,   0x8D, control, hi,    // LDA #$1E / STA $C0nB
      0xA9, 0x09,   0x8D, command, hi,    // LDA #$09 / STA $C0nA
      0xA2, 0x00,                         // LDX #$00
      0xCA,                               // DEX
      0xD0, 0xFD,                         // BNE DEX
      0xAD, status, hi,   0x85,    0x10,  // LDA $C0n9 / STA $10
      0xAD, status, hi,   0x85,    0x11,  // LDA $C0n9 / STA $11
      0xAD, status, hi,   0x85,    0x12,  // LDA $C0n9 / STA $12
      0xAD, data,   hi,   0x85,    0x13,  // LDA $C0n8 / STA $13
      0xAD, status, hi,   0x85,    0x14,  // LDA $C0n9 / STA $14
      0x4C, 0x28,   0x03};                // JMP spin
  TestFixtures::ScopedCore_t::poke(program_start, program);
}

// Control $1E and command $0B, a byte written and the status stored, a
// second byte written and the status stored, then a 9-cycle poll of bit 4.
auto poke_tdre_sequence(int slot) -> void {
  const auto hi = static_cast<uint8_t>(0xC0);
  const auto status = static_cast<uint8_t>(0x89 + (slot << 4));
  const auto data = static_cast<uint8_t>(0x88 + (slot << 4));
  const auto command = static_cast<uint8_t>(0x8A + (slot << 4));
  const auto control = static_cast<uint8_t>(0x8B + (slot << 4));
  const std::array<uint8_t, 42> program = {
      0xA9, 0x1E,   0x8D, control, hi,    // LDA #$1E / STA $C0nB
      0xA9, 0x0B,   0x8D, command, hi,    // LDA #$0B / STA $C0nA
      0xA9, 0xC1,   0x8D, data,    hi,    // LDA #$C1 / STA $C0n8 (at $030C)
      0xAD, status, hi,   0x85,    0x10,  // LDA $C0n9 / STA $10
      0xA9, 0xC2,   0x8D, data,    hi,    // LDA #$C2 / STA $C0n8
      0xAD, status, hi,   0x85,    0x11,  // LDA $C0n9 / STA $11
      0xAD, status, hi,                   // LDA $C0n9 (at $031E)
      0x29, 0x10,                         // AND #$10
      0xF0, 0xF9,                         // BEQ $031E
      0x4C, 0x25,   0x03};                // JMP spin
  TestFixtures::ScopedCore_t::poke(program_start, program);
}

}  // namespace

TEST_CASE(
    "Super Serial Card: on the 6502 a received byte reads $98, $18, $18, the "
    "byte, then $10, the status read releasing the slot's interrupt line") {
  SerialMachine_t machine;
  machine.sink.push_rx(machine.slot, 0xC1);
  poke_status_sequence(machine.slot);
  machine.run_until(program_start, status_sequence_sentinel, cycle_cap);
  REQUIRE(cpu_get_registers()->pc == status_sequence_sentinel);

  CHECK(mem[0x10] == 0x98);
  CHECK(mem[0x11] == 0x18);
  CHECK(mem[0x12] == 0x18);
  CHECK(mem[0x13] == 0xC1);
  CHECK(mem[0x14] == 0x10);
  CHECK(machine.sink.reads(machine.slot) >= 1);
  CHECK(machine.sink.bytes().empty());
}

TEST_CASE(
    "Super Serial Card: on the 6502 TDRE is set at once after a write to an "
    "idle transmitter and 1,063 cycles after it for a byte written behind") {
  SerialMachine_t machine;
  poke_tdre_sequence(machine.slot);

  CpuRegisters_t* regs = cpu_get_registers();
  regs->pc = program_start;
  regs->ps = status_interrupts_masked;
  uint64_t first_write_at = 0;
  std::vector<uint64_t> polls;
  uint32_t cycles = 0;
  while (regs->pc != tdre_sentinel && cycles < cycle_cap) {
    if (regs->pc == tdre_first_write) {
      first_write_at = cpu_get_cumulative_cycles();
    }
    if (regs->pc == tdre_poll) {
      polls.push_back(cpu_get_cumulative_cycles());
    }
    cycles += cpu_execute(0);
  }
  REQUIRE(regs->pc == tdre_sentinel);

  CHECK((mem[0x10] & 0x10) == 0x10);
  CHECK((mem[0x11] & 0x10) == 0x00);
  REQUIRE(polls.size() >= 2);
  const uint64_t boundary = first_write_at + char_9600_8n1;
  CHECK(polls.back() >= boundary);
  CHECK(polls.back() < boundary + 9);
  CHECK(polls.at(polls.size() - 2) < boundary);

  REQUIRE(machine.sink.bytes().size() == 2);
  CHECK(machine.sink.bytes().at(0).slot == machine.slot);
  CHECK(machine.sink.bytes().at(0).byte == 0xC1);
  CHECK(machine.sink.bytes().at(1).slot == machine.slot);
  CHECK(machine.sink.bytes().at(1).byte == 0xC2);
  CHECK(machine.sink.line_sets() >= 1);
  CHECK(machine.sink.last_line().baud == 9600);
  CHECK(machine.sink.last_line().data_bits == 8);
  CHECK(machine.sink.last_line().stop_half_bits == 2);
  CHECK(machine.sink.last_line().dtr == 1);
  CHECK(machine.sink.last_line().rts == 1);
}

namespace {

constexpr uint16_t switch_read_sentinel = 0x0317;
constexpr SuperSerialSwitches_t printer_mode_switches = {0x68, 0x0B};

// A write to each switch register, which the card ignores, then LDA $C0n1 /
// STA $10, LDA $C0n2 / STA $11, LDA $C0n0 / STA $12, and a spin.
auto poke_switch_reads(int slot) -> void {
  const auto hi = static_cast<uint8_t>(0xC0);
  const auto sw1 = static_cast<uint8_t>(0x81 + (slot << 4));
  const auto sw2 = static_cast<uint8_t>(0x82 + (slot << 4));
  const auto undecoded = static_cast<uint8_t>(0x80 + (slot << 4));
  const std::array<uint8_t, 26> program = {
      0xA9, 0xFF,                        // LDA #$FF
      0x8D, sw1,       hi,               // STA $C0n1
      0x8D, sw2,       hi,               // STA $C0n2
      0xAD, sw1,       hi,  0x85, 0x10,  // LDA $C0n1 / STA $10
      0xAD, sw2,       hi,  0x85, 0x11,  // LDA $C0n2 / STA $11
      0xAD, undecoded, hi,  0x85, 0x12,  // LDA $C0n0 / STA $12
      0x4C, 0x17,      0x03};            // JMP spin
  TestFixtures::ScopedCore_t::poke(program_start, program);
}

auto read_switches(SerialMachine_t& machine) -> void {
  poke_switch_reads(machine.slot);
  machine.run_until(program_start, switch_read_sentinel, cycle_cap);
  REQUIRE(cpu_get_registers()->pc == switch_read_sentinel);
}

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
