// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include "HeadlessHarness.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "apple2/peripherals/mouse/MouseRom.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "doctest.h"
#include "frontends/common/MouseFrontend.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

extern "C" unsigned mouse_abi_c_frame_size(void);
extern "C" unsigned mouse_abi_c_state_version(void);
extern "C" unsigned mouse_abi_c_button_payload_size(void);
extern "C" unsigned mouse_abi_c_move_payload_size(void);
extern "C" uint32_t mouse_abi_c_set_button_id(void);
extern "C" uint32_t mouse_abi_c_move_id(void);
extern "C" uint32_t mouse_abi_c_is_active_query_id(void);
extern "C" uint32_t mouse_abi_c_position_query_id(void);
extern "C" unsigned mouse_abi_c_position_report_size(void);

namespace {

// Through the registry, so one binary covers the built-in card and a plugin.
auto mouse_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.mouse");
}

constexpr int test_slot = 4;
constexpr size_t frame_size = 92;
constexpr uint32_t foreign_command = 0x9999;
constexpr uint32_t unknown_mouse_id = PERIPHERAL_SUBSYSTEM_MOUSE | 0x7FFF;
constexpr uint64_t ntsc_frame = 17030;
constexpr uint64_t pal_frame = 20280;

struct IrqCall_t {
  int slot;
  bool level;
};

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
    host_.ScheduleEvent = bench_schedule_event;
    s_active = this;
  }
  ~BenchHost_t() {
    if (s_active == this) {
      s_active = nullptr;
    }
  }
  BenchHost_t(const BenchHost_t&) = delete;
  auto operator=(const BenchHost_t&) -> BenchHost_t& = delete;
  BenchHost_t(BenchHost_t&&) = delete;
  auto operator=(BenchHost_t&&) -> BenchHost_t& = delete;

  auto host() -> HostInterface_t* { return &host_; }
  auto last_log() const -> const std::string& { return last_log_; }
  auto irq_calls() const -> const std::vector<IrqCall_t>& { return irq_calls_; }
  auto rom_registrations() const -> unsigned { return rom_registrations_; }
  auto last_rom() const -> const uint8_t* { return last_rom_; }
  auto set_cycles(uint64_t cycles) -> void { cycles_ = cycles; }

  auto read(void* instance, uint8_t offset) -> uint8_t {
    REQUIRE(read_c0_ != nullptr);
    return read_c0_(instance, 0, io_address(offset), 0, 0, 0);
  }
  auto write(void* instance, uint8_t offset, uint8_t value) -> void {
    REQUIRE(write_c0_ != nullptr);
    write_c0_(instance, 0, io_address(offset), 1, value, 0);
  }

 private:
  auto io_address(uint8_t offset) const -> uint16_t {
    return static_cast<uint16_t>(0xC080 + (io_slot_ << 4) + offset);
  }

  static auto bench_log(void* instance, PeripheralLogLevel level,
                        const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    if (s_active == nullptr) {
      return;
    }
    std::array<char, 256> line{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(line.data(), line.size(), fmt, args);
    va_end(args);
    s_active->last_log_ = line.data();
  }
  static auto bench_assert_irq(int slot, bool assert) -> void {
    if (s_active != nullptr) {
      s_active->irq_calls_.push_back({slot, assert});
    }
  }
  static auto bench_register_io(int slot, PeripheralIOHandler read_c0,
                                PeripheralIOHandler write_c0,
                                PeripheralIOHandler read_cx,
                                PeripheralIOHandler write_cx) -> void {
    (void)read_cx;
    (void)write_cx;
    if (s_active != nullptr) {
      s_active->io_slot_ = slot;
      s_active->read_c0_ = read_c0;
      s_active->write_c0_ = write_c0;
    }
  }
  static auto bench_register_cx_rom(int slot, const uint8_t* rom) -> void {
    (void)slot;
    if (s_active != nullptr) {
      ++s_active->rom_registrations_;
      s_active->last_rom_ = rom;
    }
  }
  static auto bench_register_expansion_rom(int slot, const uint8_t* rom)
      -> void {
    (void)slot;
    (void)rom;
  }
  static auto bench_get_cycles() -> uint64_t {
    return s_active != nullptr ? s_active->cycles_ : 0;
  }
  static auto bench_get_clock_hz() -> double { return 1020484.45; }
  static auto bench_read_floating_bus(uint32_t executed_cycles) -> uint8_t {
    (void)executed_cycles;
    return 0;
  }
  static auto bench_schedule_event(void* instance, uint64_t at_cycle) -> void {
    (void)instance;
    (void)at_cycle;
  }

  static BenchHost_t* s_active;
  HostInterface_t host_{};
  std::string last_log_;
  std::vector<IrqCall_t> irq_calls_;
  unsigned rom_registrations_ = 0;
  const uint8_t* last_rom_ = nullptr;
  uint64_t cycles_ = 0;
  int io_slot_ = 0;
  PeripheralIOHandler read_c0_ = nullptr;
  PeripheralIOHandler write_c0_ = nullptr;
};

BenchHost_t* BenchHost_t::s_active = nullptr;

class BenchCard_t {
 public:
  explicit BenchCard_t(int slot = test_slot)
      : instance_(mouse_descriptor()->init(slot, host_.host())) {}
  ~BenchCard_t() {
    if (instance_ != nullptr) {
      mouse_descriptor()->shutdown(instance_);
    }
  }
  BenchCard_t(const BenchCard_t&) = delete;
  auto operator=(const BenchCard_t&) -> BenchCard_t& = delete;
  BenchCard_t(BenchCard_t&&) = delete;
  auto operator=(BenchCard_t&&) -> BenchCard_t& = delete;

  auto instance() const -> void* { return instance_; }
  auto bench() -> BenchHost_t& { return host_; }

 private:
  BenchHost_t host_;
  void* instance_;
};

// The firmware's bank switch (bank 0 $05B-$078): CRB to DDRB, DDRB $3E, CRB to
// ORB, the bank into ORB bits 1-3 with the input bits copied from the pins.
auto bench_select_bank(BenchHost_t& bench, void* card, uint8_t bank) -> void {
  bench.write(card, 3, bench.read(card, 3) & 0xFB);
  bench.write(card, 2, 0x3E);
  bench.write(card, 3, bench.read(card, 3) | 0x04);
  bench.write(card, 2,
              static_cast<uint8_t>((bench.read(card, 2) & 0xC1) | (bank << 1)));
}

// The firmware's write handshake (bank 3 $30E-$33F): wait for PB7 low, DDRA
// out, the byte on port A, PB5 up, wait for PB7 high, PB5 down.
auto bench_send(BenchHost_t& bench, void* card, uint8_t byte) -> void {
  bench_select_bank(bench, card, 0);
  REQUIRE((bench.read(card, 2) & 0x80) == 0);
  bench.write(card, 1, bench.read(card, 1) & 0xFB);
  bench.write(card, 0, 0xFF);
  bench.write(card, 1, bench.read(card, 1) | 0x04);
  bench.write(card, 0, byte);
  bench.write(card, 2, bench.read(card, 2) | 0x20);
  REQUIRE((bench.read(card, 2) & 0x80) != 0);
  bench.write(card, 2, bench.read(card, 2) & 0xDF);
}

using Description_t = TestFixtures::ScopedTestConfig_t::Description_t;

auto mouse_key(const char* key, const char* value) -> Description_t {
  Description_t description;
  description.slots[3] = "Mockingboard";
  description.extras.push_back({"Configuration", key, value});
  return description;
}

auto mouse_in(int slot) -> bool {
  uint8_t active = 0;
  size_t size = sizeof(active);
  return peripheral_query_by_id(slot, "linapple.mouse", mouse_query_is_active,
                                &active, &size) == peripheral_ok;
}

auto card_named_in(int slot) -> std::string {
  SS_PERIPHERAL_MANIFEST manifest;
  peripheral_get_manifest(&manifest);
  return manifest.peripherals[slot].name;
}

// ScopedTestConfig_t writes every slot; a conf with no entry for one is made by
// taking the line out again.
auto remove_slot_line(const std::string& path, int slot) -> void {
  const std::string prefix = "Slot " + std::to_string(slot) + " ";
  std::vector<std::string> kept;
  {
    std::ifstream in(path);
    REQUIRE(in.is_open());
    std::string line;
    while (std::getline(in, line)) {
      if (line.compare(0, prefix.size(), prefix) != 0) {
        kept.push_back(line);
      }
    }
  }
  std::ofstream out(path, std::ios::trunc);
  REQUIRE(out.is_open());
  for (const std::string& line : kept) {
    out << line << "\n";
  }
}

struct Override_t {
  bool overrode = false;
  int slot = 0;
  std::string key_card;
  std::string displaced;
};

auto legacy_override() -> Override_t {
  Override_t result;
  const char* key_card = nullptr;
  const char* displaced = nullptr;
  result.overrode =
      peripheral_legacy_override(&result.slot, &key_card, &displaced);
  if (result.overrode) {
    result.key_card = key_card;
    result.displaced = displaced;
  }
  return result;
}

}  // namespace

TEST_CASE("Mouse card: the registry resolves the card by id and by name") {
  Peripheral_t* by_id = peripheral_find_internal("linapple.mouse");
  REQUIRE(by_id != nullptr);
  CHECK(peripheral_find_internal("Mouse Interface") == by_id);
}

TEST_CASE(
    "Mouse card: the descriptor identifies the card and carries every entry "
    "point") {
  const Peripheral_t* desc = mouse_descriptor();
  REQUIRE(desc != nullptr);
  CHECK(desc->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(desc->id) == "linapple.mouse");
  CHECK(std::string(desc->name) == "Mouse Interface");
  CHECK(desc->compatible_slots == PERIPHERAL_MASK_EXPANSION);
  CHECK(desc->default_slot == test_slot);
  CHECK(desc->init != nullptr);
  CHECK(desc->reset != nullptr);
  CHECK(desc->shutdown != nullptr);
  CHECK(desc->think != nullptr);
  CHECK(desc->on_vblank == nullptr);
  CHECK(desc->save_state != nullptr);
  CHECK(desc->load_state != nullptr);
  CHECK(desc->command != nullptr);
  CHECK(desc->query != nullptr);
}

TEST_CASE("Mouse card: a null instance or an empty host is refused") {
  const Peripheral_t* desc = mouse_descriptor();
  REQUIRE(desc != nullptr);

  CHECK(desc->init(test_slot, nullptr) == nullptr);
  HostInterface_t empty_host{};
  CHECK(desc->init(test_slot, &empty_host) == nullptr);

  desc->reset(nullptr);
  desc->shutdown(nullptr);
  desc->think(nullptr, 0);

  size_t size = 0;
  uint8_t byte = 0;
  CHECK(desc->save_state(nullptr, &byte, &size) == peripheral_error);
  CHECK(desc->load_state(nullptr, &byte, frame_size) == peripheral_error);
  CHECK(desc->command(nullptr, mouse_cmd_set_button, &byte, 1) ==
        peripheral_error);
  CHECK(desc->query(nullptr, mouse_query_is_active, &byte, &size) ==
        peripheral_error);
}

TEST_CASE(
    "Mouse card: init refuses a host lacking a member it needs, naming the "
    "member and the slot, and refuses a slot outside 1 to 7") {
  const Peripheral_t* desc = mouse_descriptor();
  REQUIRE(desc != nullptr);
  BenchHost_t bench;

  auto refused_naming = [&](const std::string& member, int slot) {
    CHECK(desc->init(slot, bench.host()) == nullptr);
    CHECK(bench.last_log().find(member) != std::string::npos);
    CHECK(bench.last_log().find("slot " + std::to_string(slot)) !=
          std::string::npos);
  };

  SUBCASE("AssertIrq") {
    bench.host()->AssertIrq = nullptr;
    refused_naming("AssertIrq", test_slot);
  }
  SUBCASE("RegisterIO") {
    bench.host()->RegisterIO = nullptr;
    refused_naming("RegisterIO", test_slot);
  }
  SUBCASE("RegisterCxROM") {
    bench.host()->RegisterCxROM = nullptr;
    refused_naming("RegisterCxROM", test_slot);
  }
  SUBCASE("GetCycles") {
    bench.host()->GetCycles = nullptr;
    refused_naming("GetCycles", test_slot);
  }
  SUBCASE("ScheduleEvent") {
    bench.host()->ScheduleEvent = nullptr;
    refused_naming("ScheduleEvent", test_slot);
  }
  SUBCASE("slot 0") { refused_naming("slots 1 to 7", 0); }
  SUBCASE("slot 8") { refused_naming("slots 1 to 7", 8); }
  SUBCASE("a complete host in slot 7 is accepted") {
    void* instance = desc->init(7, bench.host());
    REQUIRE(instance != nullptr);
    desc->shutdown(instance);
  }
}

TEST_CASE(
    "Mouse card: a command or query outside the card's subsystem, or unknown "
    "inside it, is incompatible, and a payload of the wrong size or no "
    "payload is an error") {
  BenchCard_t card;
  REQUIRE(card.instance() != nullptr);
  const Peripheral_t* desc = mouse_descriptor();

  MouseButtonPayload_t payload{0, 1, {0, 0}};
  size_t size = sizeof(payload);
  CHECK(desc->command(card.instance(), foreign_command, &payload,
                      sizeof(payload)) == peripheral_incompatible);
  CHECK(desc->query(card.instance(), foreign_command, &payload, &size) ==
        peripheral_incompatible);
  CHECK(desc->command(card.instance(), unknown_mouse_id, &payload,
                      sizeof(payload)) == peripheral_incompatible);
  CHECK(desc->query(card.instance(), unknown_mouse_id, &payload, &size) ==
        peripheral_incompatible);

  MouseMovePayload_t move{1, 1};
  CHECK(desc->command(card.instance(), mouse_cmd_move, &move,
                      sizeof(move) - 1) == peripheral_error);
  CHECK(desc->command(card.instance(), mouse_cmd_move, &move,
                      sizeof(move) + 1) == peripheral_error);
  CHECK(desc->command(card.instance(), mouse_cmd_set_button, &payload,
                      sizeof(payload) - 1) == peripheral_error);
  CHECK(desc->command(card.instance(), mouse_cmd_move, nullptr, sizeof(move)) ==
        peripheral_error);
  CHECK(desc->command(card.instance(), mouse_cmd_set_button, &payload,
                      sizeof(payload)) == peripheral_ok);
  CHECK(desc->command(card.instance(), mouse_cmd_move, &move, sizeof(move)) ==
        peripheral_ok);
}

TEST_CASE("Mouse card: save_state's sizing probe answers the frame's size") {
  BenchCard_t card;
  REQUIRE(card.instance() != nullptr);
  const Peripheral_t* desc = mouse_descriptor();

  size_t size = 0;
  CHECK(desc->save_state(card.instance(), nullptr, &size) == peripheral_ok);
  CHECK(size == frame_size);
  CHECK(size == sizeof(MouseSaveState_t));
  CHECK(mouse_abi_c_frame_size() == frame_size);
  CHECK(mouse_abi_c_state_version() == MOUSE_STATE_VERSION);

  std::array<uint8_t, frame_size> frame{};
  size = frame_size - 1;
  CHECK(desc->save_state(card.instance(), frame.data(), &size) ==
        peripheral_error);
  CHECK(size == frame_size);

  size = frame_size;
  CHECK(desc->save_state(card.instance(), frame.data(), &size) ==
        peripheral_ok);
  CHECK(size == frame_size);
  CHECK(frame.at(0) == MOUSE_STATE_VERSION);
  CHECK(frame.at(4) == frame_size);
}

TEST_CASE(
    "Mouse card: the is-active query's sizing probe and the payloads agree "
    "with the C99 view") {
  BenchCard_t card;
  REQUIRE(card.instance() != nullptr);
  const Peripheral_t* desc = mouse_descriptor();

  CHECK(mouse_abi_c_is_active_query_id() == mouse_query_is_active);
  CHECK(mouse_abi_c_set_button_id() == mouse_cmd_set_button);
  CHECK(mouse_abi_c_button_payload_size() == sizeof(MouseButtonPayload_t));
  CHECK(mouse_abi_c_move_id() == mouse_cmd_move);
  CHECK(mouse_abi_c_move_payload_size() == sizeof(MouseMovePayload_t));
  CHECK(mouse_abi_c_position_query_id() == mouse_query_position);
  CHECK(mouse_abi_c_position_report_size() == sizeof(MousePositionReport_t));

  size_t size = 0;
  CHECK(desc->query(card.instance(), mouse_query_is_active, nullptr, &size) ==
        peripheral_ok);
  CHECK(size == sizeof(uint8_t));

  uint8_t active = 0;
  size = 0;
  CHECK(desc->query(card.instance(), mouse_query_is_active, &active, &size) ==
        peripheral_error);
  CHECK(size == sizeof(uint8_t));

  size = sizeof(uint8_t);
  CHECK(desc->query(card.instance(), mouse_query_is_active, &active, &size) ==
        peripheral_ok);
  CHECK(active == 1);

  size = 0;
  CHECK(desc->query(card.instance(), mouse_query_position, nullptr, &size) ==
        peripheral_ok);
  CHECK(size == sizeof(MousePositionReport_t));

  MousePositionReport_t report{};
  size = sizeof(report) - 1;
  CHECK(desc->query(card.instance(), mouse_query_position, &report, &size) ==
        peripheral_error);
  CHECK(size == sizeof(MousePositionReport_t));

  size = sizeof(report);
  CHECK(desc->query(card.instance(), mouse_query_position, &report, &size) ==
        peripheral_ok);
  CHECK(size == sizeof(MousePositionReport_t));
  CHECK(report.tracking == 0);
}

TEST_CASE(
    "Mouse card: Mouse in slot 4 installs the card in slot 4 over the [Slots] "
    "entry, and only then") {
  SUBCASE("Slot 4 = Mockingboard with the key at 1 holds the mouse alone") {
    TestFixtures::ScopedTestConfig_t config(mouse_key("Mouse in slot 4", "1"));
    TestFixtures::ScopedCore_t core(config);
    CHECK(mouse_in(test_slot));
    CHECK(card_named_in(test_slot) == "Mouse Interface");
    mouse_frontend_initialize();
    CHECK(mouse_frontend_card_slot() == test_slot);
    const Override_t record = legacy_override();
    CHECK(record.overrode);
    CHECK(record.slot == test_slot);
    CHECK(record.key_card == "Mouse Interface");
    CHECK(record.displaced == "Mockingboard");
  }

  SUBCASE("Slot 4 = Mouse Interface with the key at 0 holds the mouse") {
    Description_t description = mouse_key("Mouse in slot 4", "0");
    description.slots[3] = "Mouse Interface";
    TestFixtures::ScopedTestConfig_t config(description);
    TestFixtures::ScopedCore_t core(config);
    CHECK(mouse_in(test_slot));
    CHECK_FALSE(legacy_override().overrode);
  }

  SUBCASE("Slot 4 = Mockingboard with the key at 0 holds no mouse") {
    TestFixtures::ScopedTestConfig_t config(mouse_key("Mouse in slot 4", "0"));
    TestFixtures::ScopedCore_t core(config);
    CHECK_FALSE(mouse_in(test_slot));
    CHECK(card_named_in(test_slot) == "Mockingboard");
    mouse_frontend_initialize();
    CHECK(mouse_frontend_card_slot() == 0);
    CHECK_FALSE(legacy_override().overrode);
  }

  SUBCASE(
      "the key at 1 with Slot 5 = Mouse Interface puts a card in 4 and in 5, "
      "and the probe takes the lower") {
    Description_t description = mouse_key("Mouse in slot 4", "1");
    description.slots[4] = "Mouse Interface";
    TestFixtures::ScopedTestConfig_t config(description);
    TestFixtures::ScopedCore_t core(config);
    CHECK(mouse_in(test_slot));
    CHECK(mouse_in(test_slot + 1));
    mouse_frontend_initialize();
    CHECK(mouse_frontend_card_slot() == test_slot);
  }

  SUBCASE("the key at 1 with no Slot 4 entry displaces the fallback") {
    TestFixtures::ScopedTestConfig_t config(mouse_key("Mouse in slot 4", "1"));
    remove_slot_line(config.path(), test_slot);
    TestFixtures::ScopedCore_t core(config);
    CHECK(mouse_in(test_slot));
    const Override_t record = legacy_override();
    CHECK(record.overrode);
    CHECK(record.displaced == "Mockingboard");
  }

  SUBCASE("the key at 1 over Slot 4 = None displaces nothing") {
    Description_t description = mouse_key("Mouse in slot 4", "1");
    description.slots[3].clear();
    TestFixtures::ScopedTestConfig_t config(description);
    TestFixtures::ScopedCore_t core(config);
    CHECK(mouse_in(test_slot));
    const Override_t record = legacy_override();
    CHECK(record.overrode);
    CHECK(record.displaced.empty());
  }

  SUBCASE("the key at 1 under its legacy spelling resolves the same") {
    TestFixtures::ScopedTestConfig_t config(mouse_key("Mouse in slot4", "1"));
    TestFixtures::ScopedCore_t core(config);
    CHECK(mouse_in(test_slot));
    CHECK(card_named_in(test_slot) == "Mouse Interface");
  }
}

namespace {

constexpr uint16_t program_start = 0x0300;
constexpr uint16_t page_copy = 0x2000;
constexpr uint32_t cycle_cap = 60000;

using Rom_t = std::array<uint8_t, mouse_rom_size>;

auto read_rom_file() -> Rom_t {
  Rom_t rom{};
  std::ifstream in(TestFixtures::get_fixture_path("roms/MouseInterface.rom"),
                   std::ios::binary);
  REQUIRE(in.is_open());
  in.read(reinterpret_cast<char*>(rom.data()),
          static_cast<std::streamsize>(rom.size()));
  REQUIRE(in.gcount() == static_cast<std::streamsize>(rom.size()));
  CHECK(in.peek() == std::ifstream::traits_type::eof());
  return rom;
}

auto describe_mouse_in(int slot) -> Description_t {
  Description_t description;
  description.slots.at(static_cast<size_t>(slot - 1)) = "Mouse Interface";
  return description;
}

struct MouseMachine_t {
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;
  int slot;

  explicit MouseMachine_t(int in_slot = test_slot)
      : config(describe_mouse_in(in_slot)), core(config), slot(in_slot) {
    peripheral_manager_init();
    linapple_register_peripherals();
    linapple_reset_hard();
  }

  auto run_until(uint16_t entry, uint16_t sentinel, uint32_t cap = cycle_cap)
      -> uint32_t {
    TestFixtures::enter_at({entry, 0, 0, 0});
    const uint32_t cycles = TestFixtures::step_until_pc(sentinel, cap);
    REQUIRE(cpu_get_registers()->pc == sentinel);
    return cycles;
  }
};

auto poke_page_copier(int slot) -> uint16_t {
  const auto slot_page = static_cast<uint8_t>(0xC0 + slot);
  const std::array<uint8_t, 14> program = {
      0xA2, 0x00,             // LDX #$00
      0xBD, 0x00, slot_page,  // LDA $Cn00,X
      0x9D, 0x00, 0x20,       // STA $2000,X
      0xE8,                   // INX
      0xD0, 0xF7,             // BNE $0302
      0x4C, 0x0B, 0x03        // JMP $030B
  };
  TestFixtures::ScopedCore_t::poke(program_start, program);
  return 0x030B;
}

// The firmware's own bank switch (bank 0 $05B-$078).
auto poke_bank_switch_and_copier(int slot, uint8_t bank) -> uint16_t {
  const auto base = static_cast<uint16_t>(0xC080 + (slot << 4));
  const auto slot_page = static_cast<uint8_t>(0xC0 + slot);
  std::vector<uint8_t> program;
  auto emit = [&program](uint8_t opcode, uint16_t operand) {
    program.push_back(opcode);
    program.push_back(static_cast<uint8_t>(operand & 0xFF));
    program.push_back(static_cast<uint8_t>(operand >> 8));
  };
  auto emit_imm = [&program](uint8_t opcode, uint8_t operand) {
    program.push_back(opcode);
    program.push_back(operand);
  };
  const auto port_b = static_cast<uint16_t>(base + 2);
  const auto control_b = static_cast<uint16_t>(base + 3);
  emit(0xAD, control_b);
  emit_imm(0x29, 0xFB);
  emit(0x8D, control_b);
  emit_imm(0xA9, 0x3E);
  emit(0x8D, port_b);
  emit(0xAD, control_b);
  emit_imm(0x09, 0x04);
  emit(0x8D, control_b);
  emit(0xAD, port_b);
  emit_imm(0x29, 0xC1);
  emit_imm(0x09, static_cast<uint8_t>(bank << 1));
  emit(0x8D, port_b);
  emit_imm(0xA2, 0x00);
  const auto copy_loop = static_cast<uint16_t>(program_start + program.size());
  emit(0xBD, static_cast<uint16_t>(slot_page << 8));
  emit(0x9D, page_copy);
  program.push_back(0xE8);
  emit_imm(0xE0, 0x08);
  program.push_back(0xD0);
  program.push_back(
      static_cast<uint8_t>(copy_loop - (program_start + program.size() + 1)));
  const auto spin = static_cast<uint16_t>(program_start + program.size());
  emit(0x4C, spin);
  TestFixtures::ScopedCore_t::poke(program_start, program.data(),
                                   program.size());
  return spin;
}

}  // namespace

TEST_CASE(
    "Mouse card: the firmware array is res/roms/MouseInterface.rom byte for "
    "byte") {
  const Rom_t rom = read_rom_file();
  size_t mismatches = 0;
  for (size_t i = 0; i < rom.size(); ++i) {
    if (rom.at(i) != mouse_rom.at(i)) {
      ++mismatches;
    }
  }
  CHECK(mismatches == 0);
}

// The signature and identification bytes: AppleMouse II User's Manual p. 43;
// Tech Note Mouse #5.
TEST_CASE(
    "Mouse card: after reset $C400-$C4FF is the ROM's bank 0 and $C4FB reads "
    "$D6") {
  MouseMachine_t machine;
  const uint16_t sentinel = poke_page_copier(machine.slot);
  machine.run_until(program_start, sentinel);

  const std::array<uint8_t, 16> signature = {0x2C, 0x58, 0xFF, 0x70, 0x1B, 0x38,
                                             0x90, 0x18, 0xB8, 0x50, 0x15, 0x01,
                                             0x20, 0xF4, 0xF4, 0xF4};
  for (size_t i = 0; i < signature.size(); ++i) {
    CHECK(mem[page_copy + i] == signature.at(i));
  }
  CHECK(mem[page_copy + 0xFB] == 0xD6);

  size_t mismatches = 0;
  for (size_t i = 0; i < mouse_rom_bank_size; ++i) {
    if (mem[page_copy + i] != mouse_rom.at(i)) {
      ++mismatches;
    }
  }
  CHECK(mismatches == 0);
}

// The table at $Cn12 holds the low bytes of the entry points (manual p. 49;
// Tech Note Mouse #1), not the entries themselves.
TEST_CASE(
    "Mouse card: $C412-$C41F is the table of the entries' low bytes, read "
    "from bank 0") {
  MouseMachine_t machine;
  const uint16_t sentinel = poke_page_copier(machine.slot);
  machine.run_until(program_start, sentinel);

  const std::array<uint8_t, 14> table = {0xB3, 0xC4, 0x9B, 0xA4, 0xC0,
                                         0x8A, 0xDD, 0xBC, 0x48, 0xF0,
                                         0x53, 0xE1, 0xE6, 0xEC};
  for (size_t i = 0; i < table.size(); ++i) {
    CAPTURE(i);
    CHECK(mem[page_copy + 0x12 + i] == table.at(i));
  }
}

// PB1-PB3 drive the ROM's A8-A10 (schematic 050-0101-A zone B3), so bank n is
// the 256 bytes at n * 256 of the file.
TEST_CASE(
    "Mouse card: the firmware's own bank switch brings each of the eight "
    "banks into $C400, and a page is registered once per bank change and "
    "never per strobe") {
  MouseMachine_t machine;
  for (uint8_t bank = 0; bank < 8; ++bank) {
    CAPTURE(bank);
    const uint16_t spin = poke_bank_switch_and_copier(machine.slot, bank);
    machine.run_until(program_start, spin);
    for (size_t i = 0; i < 8; ++i) {
      CHECK(mem[page_copy + i] ==
            mouse_rom.at(static_cast<size_t>(bank) * mouse_rom_bank_size + i));
    }
  }

  BenchCard_t card;
  REQUIRE(card.instance() != nullptr);
  BenchHost_t& bench = card.bench();
  const unsigned at_init = bench.rom_registrations();
  CHECK(at_init == 1);
  REQUIRE(bench.last_rom() != nullptr);
  CHECK(std::memcmp(bench.last_rom(), mouse_rom.data(), mouse_rom_bank_size) ==
        0);

  bench_select_bank(bench, card.instance(), 1);
  CHECK(bench.rom_registrations() == at_init + 1);
  CHECK(std::memcmp(bench.last_rom(), mouse_rom.data() + mouse_rom_bank_size,
                    mouse_rom_bank_size) == 0);

  const std::array<uint8_t, 2> strobes = {0x20, 0x10};
  for (uint8_t strobe : strobes) {
    bench.write(card.instance(), 2, bench.read(card.instance(), 2) | strobe);
    bench.write(card.instance(), 2,
                bench.read(card.instance(), 2) & static_cast<uint8_t>(~strobe));
  }
  bench_select_bank(bench, card.instance(), 1);
  CHECK(bench.rom_registrations() == at_init + 1);

  bench_select_bank(bench, card.instance(), 5);
  CHECK(bench.rom_registrations() == at_init + 2);
  CHECK(std::memcmp(bench.last_rom(),
                    mouse_rom.data() + 5 * mouse_rom_bank_size,
                    mouse_rom_bank_size) == 0);
}

namespace {

constexpr uint16_t indirect_jump = 0x03F0;
constexpr uint16_t meter_loop = 0x03E0;
constexpr uint16_t handler_start = 0x0380;
constexpr uint16_t meter_low_table = 0x2000;
constexpr uint16_t meter_high_table = 0x2100;
constexpr uint16_t meter_vbl_table = 0x2200;
constexpr uint16_t meter_flags_table = 0x2300;
constexpr uint16_t meter_return_table = 0x2400;
constexpr uint8_t meter_low = 0x06;
constexpr uint8_t meter_high = 0x09;
constexpr uint8_t meter_index = 0x0A;
constexpr uint8_t storm_low = 0x0B;
constexpr uint8_t storm_high = 0x0C;
constexpr uint32_t firmware_cycle_cap = 200000;
constexpr uint32_t lead_in_cycles = 8000;

enum FirmwareEntry_t {
  entry_set_mouse = 0,
  entry_serve_mouse = 1,
  entry_read_mouse = 2,
  entry_clear_mouse = 3,
  entry_pos_mouse = 4,
  entry_clamp_mouse = 5,
  entry_home_mouse = 6,
  entry_init_mouse = 7,
  entry_peek_poke = 8,
  entry_time_data = 10,
  entry_data_byte = 11
};

struct Call_t {
  int entry;
  uint8_t a;
};

// LDA $Cn12+k / STA $07 / LDA #$Cn / STA $08 / LDA #a / LDX #x / LDY #y /
// JSR $03F0, with JMP ($0007) at $03F0. X and Y are $Cn and $n0 unless a case
// proves the firmware derives them itself.
auto emit_firmware_call(std::vector<uint8_t>& program, int slot, int entry,
                        uint8_t a, bool x_y_zero = false) -> void {
  const auto page = static_cast<uint8_t>(0xC0 + slot);
  const auto table = static_cast<uint16_t>((page << 8) + 0x12 + entry);
  const uint8_t x = x_y_zero ? 0 : page;
  const uint8_t y = x_y_zero ? 0 : static_cast<uint8_t>(slot << 4);
  const std::vector<uint8_t> call = {0xAD,
                                     static_cast<uint8_t>(table & 0xFF),
                                     static_cast<uint8_t>(table >> 8),
                                     0x85,
                                     0x07,
                                     0xA9,
                                     page,
                                     0x85,
                                     0x08,
                                     0xA9,
                                     a,
                                     0xA2,
                                     x,
                                     0xA0,
                                     y,
                                     0x20,
                                     static_cast<uint8_t>(indirect_jump & 0xFF),
                                     static_cast<uint8_t>(indirect_jump >> 8)};
  program.insert(program.end(), call.begin(), call.end());
}

auto poke_indirect_jump() -> void {
  const std::array<uint8_t, 3> jump = {0x6C, 0x07, 0x00};
  TestFixtures::ScopedCore_t::poke(indirect_jump, jump);
}

auto poke_calls(int slot, const std::vector<Call_t>& calls) -> uint16_t {
  std::vector<uint8_t> program;
  for (const Call_t& call : calls) {
    emit_firmware_call(program, slot, call.entry, call.a);
  }
  const auto spin = static_cast<uint16_t>(program_start + program.size());
  program.push_back(0x4C);
  program.push_back(static_cast<uint8_t>(spin & 0xFF));
  program.push_back(static_cast<uint8_t>(spin >> 8));
  REQUIRE(spin + 3 <= handler_start);
  TestFixtures::ScopedCore_t::poke(program_start, program.data(),
                                   program.size());
  poke_indirect_jump();
  return spin;
}

struct StepResult_t {
  bool completed;
  bool carry;
  uint32_t cycles;
};

auto step_firmware(int slot, int entry, uint8_t a) -> StepResult_t {
  const uint16_t spin = poke_calls(slot, {{entry, a}});
  TestFixtures::enter_at({program_start, 0, 0, 0});
  const uint32_t cycles = TestFixtures::step_until_pc(spin, firmware_cycle_cap);
  return {cpu_get_registers()->pc == spin,
          (cpu_get_registers()->ps & 0x01) != 0, cycles};
}

auto call_firmware(int slot, int entry, uint8_t a) -> bool {
  const StepResult_t result = step_firmware(slot, entry, a);
  REQUIRE(result.completed);
  return result.carry;
}

// $06 and $09 count turns of 8 cycles (15 at a wrap of $06); the handler files
// the count, $C019, the interrupted return address and SERVEMOUSE's flags per
// entry. An interrupt taken between the INC that wrapped $06 and the INC of $09
// sees a high byte one short, which the return address lets the reader
// correct. SERVEMOUSE keeps $06 in Y across its own use of it as an RTS (bank 0
// $0C4-$0CC), so the counter survives the call.
auto poke_meter(int slot, bool serve = true, bool x_y_zero = false) -> void {
  const std::vector<uint8_t> loop = {
      0x58,                   // CLI
      0xE6, meter_low,        // INC $06
      0xD0, 0xFC,             // BNE $03E1
      0xE6, meter_high,       // INC $09
      0x4C, 0xE1,       0x03  // JMP $03E1
  };
  TestFixtures::ScopedCore_t::poke(meter_loop, loop.data(), loop.size());

  std::vector<uint8_t> handler;
  if (serve) {
    handler = {
        0xA6,
        meter_index,  // LDX $0A
        0xA5,
        meter_low,  // LDA $06
        0x9D,
        static_cast<uint8_t>(meter_low_table & 0xFF),
        static_cast<uint8_t>(meter_low_table >> 8),  // STA $2000,X
        0xA5,
        meter_high,  // LDA $09
        0x9D,
        static_cast<uint8_t>(meter_high_table & 0xFF),
        static_cast<uint8_t>(meter_high_table >> 8),  // STA $2100,X
        0xAD,
        0x19,
        0xC0,  // LDA $C019
        0x9D,
        static_cast<uint8_t>(meter_vbl_table & 0xFF),
        static_cast<uint8_t>(meter_vbl_table >> 8),  // STA $2200,X
        0x8A,                                        // TXA
        0xA8,                                        // TAY
        0xBA,                                        // TSX
        0xBD,
        0x02,
        0x01,  // LDA $0102,X: the pushed return address, low byte
        0x99,
        static_cast<uint8_t>(meter_return_table & 0xFF),
        static_cast<uint8_t>(meter_return_table >> 8),  // STA $2400,Y
        0xE6,
        meter_index  // INC $0A
    };
    emit_firmware_call(handler, slot, entry_serve_mouse, 0, x_y_zero);
    const std::vector<uint8_t> tail = {
        0x08,  // PHP
        0x68,  // PLA
        0xA6,
        meter_index,  // LDX $0A
        0xCA,         // DEX
        0x9D,
        static_cast<uint8_t>(meter_flags_table & 0xFF),
        static_cast<uint8_t>(meter_flags_table >> 8),  // STA $2300,X
        0x40                                           // RTI
    };
    handler.insert(handler.end(), tail.begin(), tail.end());
  } else {
    handler = {
        0xE6, storm_low,   // INC $0B
        0xD0, 0x02,        // BNE +2
        0xE6, storm_high,  // INC $0C
        0x40               // RTI
    };
  }
  REQUIRE(handler_start + handler.size() <= meter_loop);
  TestFixtures::ScopedCore_t::poke(handler_start, handler.data(),
                                   handler.size());
  poke_indirect_jump();

  const std::array<uint8_t, 2> vector = {
      static_cast<uint8_t>(handler_start & 0xFF),
      static_cast<uint8_t>(handler_start >> 8)};
  TestFixtures::ScopedCore_t::poke(IRQ_VECTOR_ADDR, vector);

  const std::array<uint8_t, 1> zero = {0};
  for (uint8_t at :
       {meter_low, meter_high, meter_index, storm_low, storm_high}) {
    TestFixtures::ScopedCore_t::poke(at, zero);
  }
  std::vector<uint8_t> blank(1280, 0);
  TestFixtures::ScopedCore_t::poke(meter_low_table, blank.data(), blank.size());
}

// 8 cycles a turn, 7 more at every wrap of the low byte; the count wraps after
// 65,536 turns, so two entries are compared modulo that.
constexpr uint64_t meter_wrap = 8 * 65536 + 7 * 256;

auto meter_delta(uint64_t from, uint64_t to) -> uint64_t {
  return (to + meter_wrap - from) % meter_wrap;
}

auto meter_entries() -> std::vector<uint64_t> {
  constexpr uint8_t inc_high_byte = (meter_loop + 5) & 0xFF;
  std::vector<uint64_t> cycles;
  for (size_t i = 0; i < mem[meter_index]; ++i) {
    uint64_t turns = mem[meter_low_table + i] +
                     (static_cast<uint64_t>(mem[meter_high_table + i]) << 8);
    if (mem[meter_return_table + i] == inc_high_byte) {
      turns += 256;
    }
    cycles.push_back(8 * turns + 7 * (turns >> 8));
  }
  return cycles;
}

auto meter_vbl(size_t entry) -> uint8_t { return mem[meter_vbl_table + entry]; }

auto meter_serve_carry(size_t entry) -> bool {
  return (mem[meter_flags_table + entry] & 0x01) != 0;
}

auto storm_count() -> unsigned {
  return mem[storm_low] | (static_cast<unsigned>(mem[storm_high]) << 8);
}

// Interrupt to RTI, SERVEMOUSE included: a regression pin of this emulator,
// measured from a sixty-entry run.
constexpr uint64_t serve_handler_cycles = 456;

// Entry i stands at its count plus the i handlers the loop did not count. Held
// against i periods from the first entry, a period one cycle off drifts across
// the run, which consecutive gaps could never show.
auto check_period(const std::vector<uint64_t>& entries, uint64_t period)
    -> void {
  REQUIRE(entries.size() >= 2);
  constexpr uint64_t tolerance = 32;
  for (size_t i = 1; i < entries.size(); ++i) {
    CAPTURE(i);
    const uint64_t at =
        meter_delta(entries.at(0), entries.at(i)) + i * serve_handler_cycles;
    const uint64_t expected = (i * period) % meter_wrap;
    const uint64_t drift = (at + meter_wrap - expected) % meter_wrap;
    CAPTURE(drift);
    CHECK((drift <= tolerance || drift >= meter_wrap - tolerance));
  }
}

// The cycles of the frame already drawn, found from the blanking flag the IOU
// raises at line 192 (IIe Technical Reference p. 170).
auto video_frame_phase() -> uint32_t {
  const uint32_t period = system_state.clks_per_frame;
  REQUIRE(period > 0);
  for (uint32_t k = 0; k < period; ++k) {
    if (video_get_vbl(k) && !video_get_vbl(k + 1)) {
      return (period - 1 - k) % period;
    }
  }
  REQUIRE(false);
  return 0;
}

// Frames run at the machine's own length, so a PAL machine gets 20,280 cycles.
struct MouseSession_t {
  TestFixtures::ScopedTestConfig_t config;
  HeadlessHarness_t harness;
  int slot;

  explicit MouseSession_t(int in_slot = test_slot, bool pal = false)
      : MouseSession_t(describe(in_slot, pal), in_slot) {}

  MouseSession_t(const Description_t& description, int in_slot)
      : config(description), harness(config), slot(in_slot) {
    harness.boot();
  }

  static auto describe(int in_slot, bool pal) -> Description_t {
    Description_t description = describe_mouse_in(in_slot);
    if (pal) {
      description.extras.push_back({"Configuration", "Video Emulation", "2"});
    }
    return description;
  }

  auto frame_length() const -> uint64_t { return system_state.clks_per_frame; }

  auto run_frames(uint32_t frames) -> void {
    for (uint32_t i = 0; i < frames; ++i) {
      linapple_run_frame(system_state.clks_per_frame);
    }
  }

  // A partial frame first by default: a tick free-running from reset stands at
  // a frame's end, and the stepped calls' few hundred cycles would otherwise
  // leave the handler straddling the boundary.
  auto run_metered_frames(uint32_t frames, uint32_t lead_in = lead_in_cycles)
      -> std::vector<uint64_t> {
    TestFixtures::enter_at({meter_loop, 0, 0, 0});
    const uint64_t start = cpu_get_cumulative_cycles();
    if (lead_in > 0) {
      linapple_run_frame(lead_in);
    }
    run_frames(frames);
    last_total_cycles = cpu_get_cumulative_cycles() - start;
    uint64_t turns =
        mem[meter_low] | (static_cast<uint64_t>(mem[meter_high]) << 8);
    if (cpu_get_registers()->pc == meter_loop + 5) {
      turns += 256;
    }
    last_loop_cycles = 8 * turns + 7 * (turns >> 8);
    return meter_entries();
  }

  uint64_t last_total_cycles = 0;
  uint64_t last_loop_cycles = 0;

  auto align_to_video_frame() -> void {
    const std::array<uint8_t, 3> spin = {0x4C, 0x00, 0x03};
    TestFixtures::ScopedCore_t::poke(program_start, spin);
    TestFixtures::enter_at({program_start, 0, 0, 0});
    const uint32_t phase = video_frame_phase();
    if (phase != 0) {
      linapple_run_frame(system_state.clks_per_frame - phase);
    }
  }

  // For entries that need the scanner: INITMOUSE waits on $C019.
  auto run_calls_in_frames(const std::vector<Call_t>& calls, uint32_t frames)
      -> bool {
    const uint16_t spin = poke_calls(slot, calls);
    TestFixtures::enter_at({program_start, 0, 0, 0});
    run_frames(frames);
    REQUIRE(cpu_get_registers()->pc == spin);
    return (cpu_get_registers()->ps & 0x01) != 0;
  }
};

// The batch total less the loop time, which wrapped at most once a run, over
// the entries.
auto measured_handler_cycles(const MouseSession_t& session, size_t entries)
    -> uint64_t {
  REQUIRE(entries > 0);
  const uint64_t wraps =
      (session.last_total_cycles - session.last_loop_cycles + meter_wrap / 2) /
      meter_wrap;
  const uint64_t loop_cycles = session.last_loop_cycles + wraps * meter_wrap;
  return (session.last_total_cycles - 2 - loop_cycles + entries / 2) / entries;
}

auto press_button(int slot, bool down, uint8_t button = 0) -> void {
  MouseButtonPayload_t payload{
      button, static_cast<uint8_t>(down ? 1 : 0), {0, 0}};
  REQUIRE(peripheral_command(slot, mouse_cmd_set_button, &payload,
                             sizeof(payload)) == peripheral_ok);
  peripheral_manager_think(0);
}

auto move_mouse(int slot, int32_t dx, int32_t dy) -> void {
  MouseMovePayload_t payload{dx, dy};
  REQUIRE(peripheral_command(slot, mouse_cmd_move, &payload, sizeof(payload)) ==
          peripheral_ok);
  peripheral_manager_think(0);
}

struct Reading_t {
  int16_t x;
  int16_t y;
  uint8_t status;
};

auto word_at(uint16_t low, uint16_t high) -> int16_t {
  return static_cast<int16_t>(static_cast<uint16_t>(
      mem[low] | (static_cast<uint16_t>(mem[high]) << 8)));
}

// READMOUSE through the table, then the slot's holes (manual p. 44).
auto read_mouse(int slot) -> Reading_t {
  REQUIRE_FALSE(call_firmware(slot, entry_read_mouse, 0));
  const auto n = static_cast<uint16_t>(slot);
  Reading_t reading{};
  reading.x = word_at(0x478 + n, 0x578 + n);
  reading.y = word_at(0x4F8 + n, 0x5F8 + n);
  reading.status = mem[0x778 + n];
  return reading;
}

auto poke_byte(uint16_t at, uint8_t value) -> void {
  const std::array<uint8_t, 1> byte = {value};
  TestFixtures::ScopedCore_t::poke(at, byte);
}

// CLAMPMOUSE and the peek take their bytes from the slot-0 holes (manual
// p. 48; Tech Note Mouse #7).
auto poke_slot0_holes(uint8_t at_478, uint8_t at_4f8, uint8_t at_578,
                      uint8_t at_5f8) -> void {
  poke_byte(0x478, at_478);
  poke_byte(0x4F8, at_4f8);
  poke_byte(0x578, at_578);
  poke_byte(0x5F8, at_5f8);
}

auto poke_slot_holes(int slot, int16_t x, int16_t y) -> void {
  const auto n = static_cast<uint16_t>(slot);
  poke_byte(0x478 + n, static_cast<uint8_t>(x & 0xFF));
  poke_byte(0x578 + n, static_cast<uint8_t>(static_cast<uint16_t>(x) >> 8));
  poke_byte(0x4F8 + n, static_cast<uint8_t>(y & 0xFF));
  poke_byte(0x5F8 + n, static_cast<uint8_t>(static_cast<uint16_t>(y) >> 8));
}

// The firmware's own write handshake (bank 3 $30E-$33F), so a byte reaches the
// 6805 with no firmware entry in between.
auto send_raw_byte(MouseMachine_t& machine, uint8_t byte) -> void {
  const auto base = static_cast<uint16_t>(0xC080 + (machine.slot << 4));
  std::vector<uint8_t> program;
  auto emit = [&program](uint8_t opcode, uint16_t operand) {
    program.push_back(opcode);
    program.push_back(static_cast<uint8_t>(operand & 0xFF));
    program.push_back(static_cast<uint8_t>(operand >> 8));
  };
  auto emit_imm = [&program](uint8_t opcode, uint8_t operand) {
    program.push_back(opcode);
    program.push_back(operand);
  };
  const uint16_t port_a = base;
  const auto control_a = static_cast<uint16_t>(base + 1);
  const auto port_b = static_cast<uint16_t>(base + 2);
  const auto control_b = static_cast<uint16_t>(base + 3);
  emit(0xAD, control_b);
  emit_imm(0x29, 0xFB);
  emit(0x8D, control_b);
  emit_imm(0xA9, 0x3E);
  emit(0x8D, port_b);
  emit(0xAD, control_b);
  emit_imm(0x09, 0x04);
  emit(0x8D, control_b);
  emit(0xAD, control_a);
  emit_imm(0x29, 0xFB);
  emit(0x8D, control_a);
  emit_imm(0xA9, 0xFF);
  emit(0x8D, port_a);
  emit(0xAD, control_a);
  emit_imm(0x09, 0x04);
  emit(0x8D, control_a);
  emit(0xAD, port_b);
  emit_imm(0x30, 0xFB);
  emit_imm(0xA9, byte);
  emit(0x8D, port_a);
  emit(0xAD, port_b);
  emit_imm(0x09, 0x20);
  emit(0x8D, port_b);
  emit(0xAD, port_b);
  emit_imm(0x10, 0xFB);
  emit(0xAD, port_b);
  emit_imm(0x29, 0xDF);
  emit(0x8D, port_b);
  const auto spin = static_cast<uint16_t>(program_start + program.size());
  emit(0x4C, spin);
  TestFixtures::ScopedCore_t::poke(program_start, program.data(),
                                   program.size());
  machine.run_until(program_start, spin);
}

// LDA abs / STA zp for each address, into $10 upwards.
auto read_addresses(MouseMachine_t& machine,
                    const std::vector<uint16_t>& addresses)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> program;
  uint8_t zero_page = 0x10;
  for (uint16_t address : addresses) {
    program.push_back(0xAD);
    program.push_back(static_cast<uint8_t>(address & 0xFF));
    program.push_back(static_cast<uint8_t>(address >> 8));
    program.push_back(0x85);
    program.push_back(zero_page++);
  }
  const auto spin = static_cast<uint16_t>(program_start + program.size());
  program.push_back(0x4C);
  program.push_back(static_cast<uint8_t>(spin & 0xFF));
  program.push_back(static_cast<uint8_t>(spin >> 8));
  TestFixtures::ScopedCore_t::poke(program_start, program.data(),
                                   program.size());
  machine.run_until(program_start, spin);
  std::vector<uint8_t> values;
  zero_page = 0x10;
  for (size_t i = 0; i < addresses.size(); ++i) {
    values.push_back(mem[zero_page++]);
  }
  return values;
}

// The firmware's bank switch (bank 0 $05B-$078), the bank left selected.
auto select_bank_stepped(int slot, uint8_t bank) -> void {
  const auto base = static_cast<uint16_t>(0xC080 + (slot << 4));
  std::vector<uint8_t> program;
  auto emit = [&program](uint8_t opcode, uint16_t operand) {
    program.push_back(opcode);
    program.push_back(static_cast<uint8_t>(operand & 0xFF));
    program.push_back(static_cast<uint8_t>(operand >> 8));
  };
  auto emit_imm = [&program](uint8_t opcode, uint8_t operand) {
    program.push_back(opcode);
    program.push_back(operand);
  };
  const auto port_b = static_cast<uint16_t>(base + 2);
  const auto control_b = static_cast<uint16_t>(base + 3);
  emit(0xAD, control_b);
  emit_imm(0x29, 0xFB);
  emit(0x8D, control_b);
  emit_imm(0xA9, 0x3E);
  emit(0x8D, port_b);
  emit(0xAD, control_b);
  emit_imm(0x09, 0x04);
  emit(0x8D, control_b);
  emit(0xAD, port_b);
  emit_imm(0x29, 0xC1);
  emit_imm(0x09, static_cast<uint8_t>(bank << 1));
  emit(0x8D, port_b);
  const auto spin = static_cast<uint16_t>(program_start + program.size());
  emit(0x4C, spin);
  TestFixtures::ScopedCore_t::poke(program_start, program.data(),
                                   program.size());
  TestFixtures::enter_at({program_start, 0, 0, 0});
  TestFixtures::step_until_pc(spin, cycle_cap);
  REQUIRE(cpu_get_registers()->pc == spin);
}

}  // namespace

// Manual pp. 44, 47; bank 0 $0B3: CMP #$10 / BCS.
TEST_CASE(
    "Mouse card: SETMOUSE refuses a mode of $10 or more with the carry set "
    "and takes one below it into $7FC") {
  MouseMachine_t machine;
  poke_byte(0x7FC, 0x55);
  CHECK(call_firmware(machine.slot, entry_set_mouse, 0x10));
  CHECK(mem[0x7FC] == 0x55);
  CHECK(call_firmware(machine.slot, entry_set_mouse, 0xFF));
  CHECK(mem[0x7FC] == 0x55);
  CHECK_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x0F));
  CHECK(mem[0x7FC] == 0x0F);
  CHECK_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x00));
  CHECK(mem[0x7FC] == 0x00);
}

// The tick free-runs from reset at one frame's length, so a handler installed
// after SETMOUSE sees one entry a frame, late in the frame.
TEST_CASE(
    "Mouse card: the screen-refresh interrupt comes once a frame with bit 3 "
    "set whatever bit 0 says, SERVEMOUSE reports $08 and a second call finds "
    "nothing") {
  uint8_t mode = 0x09;
  SUBCASE("mouse on") { mode = 0x09; }
  SUBCASE("mouse off") { mode = 0x08; }

  MouseSession_t session;
  CHECK_FALSE(call_firmware(session.slot, entry_set_mouse, mode));
  poke_meter(session.slot);
  const std::vector<uint64_t> entries = session.run_metered_frames(60);
  CHECK(entries.size() >= 59);
  CHECK(entries.size() <= 61);
  CHECK((mem[0x77C] & 0x0E) == 0x08);
  REQUIRE(entries.size() >= 2);
  CHECK(entries.at(0) > 4000);
  check_period(entries, ntsc_frame);
  CHECK(call_firmware(session.slot, entry_serve_mouse, 0));
}

TEST_CASE(
    "Mouse card: a button change interrupts at the tick, not on arrival, and "
    "only while the mouse is on") {
  SUBCASE("mode $05: one entry at the tick, reporting the button") {
    MouseSession_t session;
    CHECK_FALSE(call_firmware(session.slot, entry_set_mouse, 0x05));
    poke_meter(session.slot);
    press_button(session.slot, true);
    std::vector<uint64_t> entries = session.run_metered_frames(1);
    REQUIRE(entries.size() == 1);
    CHECK(entries.at(0) > 4000);
    CHECK((mem[0x77C] & 0x0E) == 0x04);
    entries = session.run_metered_frames(1);
    CHECK(entries.size() == 1);
  }

  SUBCASE("mode $04: the button raises nothing while the mouse is off") {
    MouseSession_t session;
    CHECK_FALSE(call_firmware(session.slot, entry_set_mouse, 0x04));
    poke_meter(session.slot);
    press_button(session.slot, true);
    const std::vector<uint64_t> entries = session.run_metered_frames(2);
    CHECK(entries.empty());
  }
}

// On a IIe the firmware waits for three edges of $C019 (bank 2 $226-$234)
// before the second $50, so INITMOUSE runs under the frame loop. It "sets the
// internal default values" (manual p. 48). The 6502 side keeps the mode's low
// nibble in the hole with $40 above it (bank 2 $200-$209), so the hole does
// not show the 6805's mode; a tick that enters no handler does.
TEST_CASE(
    "Mouse card: INITMOUSE on an Enhanced //e returns with the carry clear, "
    "reads one reply byte into $6FC, and leaves the 6805 off at (0, 0) with "
    "the clamps 0..1023") {
  MouseSession_t session;
  REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x01));
  poke_slot0_holes(0x64, 0xC8, 0x00, 0x00);
  REQUIRE_FALSE(call_firmware(session.slot, entry_clamp_mouse, 0));
  poke_slot0_holes(0x2C, 0x90, 0x01, 0x01);
  REQUIRE_FALSE(call_firmware(session.slot, entry_clamp_mouse, 1));
  poke_slot_holes(session.slot, 150, 350);
  REQUIRE_FALSE(call_firmware(session.slot, entry_pos_mouse, 0));
  Reading_t reading = read_mouse(session.slot);
  REQUIRE(reading.x == 150);
  REQUIRE(reading.y == 350);
  REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x09));

  // The reply's value is unsourced; only that one byte was stored is pinned.
  poke_byte(0x6FC, 0x00);
  session.align_to_video_frame();
  CHECK_FALSE(session.run_calls_in_frames({{entry_init_mouse, 0}}, 3));
  CHECK(mem[0x6FC] != 0x00);
  CHECK(mem[0x7FC] == 0x49);

  poke_meter(session.slot);
  const std::vector<uint64_t> entries = session.run_metered_frames(3, 0);
  CHECK(entries.empty());

  REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x01));
  reading = read_mouse(session.slot);
  CHECK(reading.x == 0);
  CHECK(reading.y == 0);
  move_mouse(session.slot, 2000, 2000);
  reading = read_mouse(session.slot);
  CHECK(reading.x == 1023);
  CHECK(reading.y == 1023);
}

TEST_CASE(
    "Mouse card: CLAMPMOUSE takes its bytes in the firmware's order, as signed "
    "values, moves nothing, pins at a minimum above the maximum, and "
    "HOMEMOUSE goes to the lower boundaries") {
  MouseMachine_t machine;
  REQUIRE_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x01));

  poke_slot0_holes(0x64, 0xC8, 0x00, 0x00);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_clamp_mouse, 0));
  Reading_t reading = read_mouse(machine.slot);
  CHECK(reading.x == 0);
  CHECK(reading.y == 0);
  move_mouse(machine.slot, 2000, 0);
  CHECK(read_mouse(machine.slot).x == 200);
  move_mouse(machine.slot, -2000, 0);
  CHECK(read_mouse(machine.slot).x == 100);

  poke_slot0_holes(0x2C, 0x90, 0x01, 0x01);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_clamp_mouse, 1));
  move_mouse(machine.slot, 0, 2000);
  CHECK(read_mouse(machine.slot).y == 400);
  move_mouse(machine.slot, 0, -2000);
  CHECK(read_mouse(machine.slot).y == 300);

  move_mouse(machine.slot, 50, 50);
  reading = read_mouse(machine.slot);
  CHECK(reading.x == 150);
  CHECK(reading.y == 350);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_home_mouse, 0));
  reading = read_mouse(machine.slot);
  CHECK(reading.x == 100);
  CHECK(reading.y == 300);

  poke_slot0_holes(0x00, 0xFF, 0xFF, 0x00);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_clamp_mouse, 0));
  move_mouse(machine.slot, -1000, 0);
  reading = read_mouse(machine.slot);
  CHECK(reading.x == -256);
  CHECK(mem[0x47C] == 0x00);
  CHECK(mem[0x57C] == 0xFF);
  move_mouse(machine.slot, 156, 0);
  CHECK(read_mouse(machine.slot).x == -100);

  // A minimum of 200 over a maximum of 100 pins at 200.
  poke_slot0_holes(0xC8, 0x64, 0x00, 0x00);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_clamp_mouse, 0));
  CHECK(read_mouse(machine.slot).x == -100);
  move_mouse(machine.slot, 1, 0);
  CHECK(read_mouse(machine.slot).x == 200);
  move_mouse(machine.slot, -1000, 0);
  CHECK(read_mouse(machine.slot).x == 200);
}

// Manual p. 47; a loaded value is taken as given and only motion clamps (IIc
// Technical Reference Table 9-3: CLEARMOUSE's zero is "not necessarily within
// clamping boundaries").
TEST_CASE(
    "Mouse card: POSMOUSE loads the position from the slot's holes unclamped, "
    "and the next motion clamps it") {
  MouseMachine_t machine;
  REQUIRE_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x01));
  poke_slot_holes(machine.slot, 560, 192);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_pos_mouse, 0));
  Reading_t reading = read_mouse(machine.slot);
  CHECK(reading.x == 560);
  CHECK(reading.y == 192);

  poke_slot0_holes(0x00, 0x17, 0x00, 0x01);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_clamp_mouse, 0));
  poke_slot_holes(machine.slot, 300, 100);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_pos_mouse, 0));
  reading = read_mouse(machine.slot);
  CHECK(reading.x == 300);
  CHECK(reading.y == 100);
  move_mouse(machine.slot, 1, 0);
  CHECK(read_mouse(machine.slot).x == 279);
}

// "The button and interrupt status byte remains unchanged" (manual p. 47); only
// INITMOUSE resets the mode and clamps (Tech Note Mouse #3). The firmware
// zeroes the four holes itself (bank 3 $349-$356).
TEST_CASE(
    "Mouse card: CLEARMOUSE zeroes the position and the holes and leaves the "
    "mode, the clamps and a pending movement alone") {
  MouseSession_t session;
  REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x03));
  poke_slot0_holes(0x64, 0xC8, 0x00, 0x00);
  REQUIRE_FALSE(call_firmware(session.slot, entry_clamp_mouse, 0));
  move_mouse(session.slot, 50, 50);
  poke_slot_holes(session.slot, -1, -1);
  REQUIRE_FALSE(call_firmware(session.slot, entry_clear_mouse, 0));
  CHECK(mem[0x47C] == 0);
  CHECK(mem[0x57C] == 0);
  CHECK(mem[0x4FC] == 0);
  CHECK(mem[0x5FC] == 0);

  Reading_t reading = read_mouse(session.slot);
  CHECK(reading.x == 0);
  CHECK(reading.y == 0);
  CHECK(mem[0x7FC] == 0x03);

  poke_meter(session.slot);
  const std::vector<uint64_t> entries = session.run_metered_frames(1);
  CHECK(entries.size() == 1);
  CHECK((mem[0x77C] & 0x0E) == 0x02);

  move_mouse(session.slot, 50, 0);
  CHECK(read_mouse(session.slot).x == 100);
}

// Status bits: manual p. 45; one button: schematic SW on J1-4. Bits 1-3 read 0
// in the hole (manual p. 47) and the 6805 forgets the sources with them
// (inferred), while the line stays up until SERVEMOUSE (Tech Note Mouse #4).
TEST_CASE(
    "Mouse card: READMOUSE writes the status byte whole, a second button "
    "changes nothing, and the interrupt bits read 0 while the line stays up") {
  SUBCASE("movement and the button, one reading at a time") {
    MouseMachine_t machine;
    REQUIRE_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x01));
    move_mouse(machine.slot, 5, 5);
    CHECK(read_mouse(machine.slot).status == 0x20);
    CHECK(read_mouse(machine.slot).status == 0x00);
    press_button(machine.slot, true);
    CHECK(read_mouse(machine.slot).status == 0x80);
    CHECK(read_mouse(machine.slot).status == 0xC0);
    press_button(machine.slot, false);
    CHECK(read_mouse(machine.slot).status == 0x40);
    CHECK(read_mouse(machine.slot).status == 0x00);
    press_button(machine.slot, true, 1);
    CHECK(read_mouse(machine.slot).status == 0x00);
    move_mouse(machine.slot, 1, 0);
    press_button(machine.slot, true);
    CHECK(read_mouse(machine.slot).status == 0xA0);
  }

  SUBCASE("after a tick, READMOUSE clears the sources and leaves the line") {
    MouseSession_t session;
    REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x0B));
    move_mouse(session.slot, 3, 0);
    const uint16_t spin = poke_calls(session.slot, {});
    TestFixtures::enter_at({spin, 0, 0, 0});
    session.run_frames(2);
    const Reading_t reading = read_mouse(session.slot);
    CHECK((reading.status & 0x0E) == 0x00);
    CHECK((reading.status & 0x20) == 0x20);

    // The line is still up, so the handler is entered at the CLI and finds
    // nothing; the frame's own tick follows with the refresh bit.
    poke_meter(session.slot);
    const std::vector<uint64_t> entries = session.run_metered_frames(1, 0);
    REQUIRE(entries.size() == 2);
    CHECK(entries.at(0) < 200);
    CHECK(meter_serve_carry(0));
    CHECK_FALSE(meter_serve_carry(1));
    CHECK((mem[0x77C] & 0x0E) == 0x08);
  }
}

// SERVEMOUSE "sets C to 0 if the interrupt was caused by the mouse" (manual
// p. 47), the carry coming from bits 1-3 of the reply alone (bank 3 $3BD-$3D4);
// the entry derives X and Y itself (bank 0 $0C4-$0D8).
TEST_CASE(
    "Mouse card: SERVEMOUSE from a handler with X and Y zero reports the "
    "sources once over READMOUSE's bits, releases the line, and the second "
    "call answers not the mouse") {
  SUBCASE("served") {
    MouseSession_t session;
    REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x0B));
    press_button(session.slot, true);
    CHECK(read_mouse(session.slot).status == 0x80);
    move_mouse(session.slot, 3, 0);
    poke_meter(session.slot, true, true);
    std::vector<uint64_t> entries = session.run_metered_frames(1);
    REQUIRE(entries.size() == 1);
    CHECK_FALSE(meter_serve_carry(0));
    CHECK(mem[0x77C] == 0x8A);
    CHECK(call_firmware(session.slot, entry_serve_mouse, 0));

    poke_meter(session.slot, true, true);
    entries = session.run_metered_frames(1, 0);
    REQUIRE(entries.size() == 1);
    CHECK(mem[0x77C] == 0x88);
  }

  SUBCASE("a handler that never serves is re-entered every instruction") {
    MouseSession_t session;
    REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x08));
    poke_meter(session.slot, false);
    session.run_metered_frames(1);
    CHECK(storm_count() > 10);
  }
}

// The interrupt comes "at the end of the current monitor screen writing cycle"
// (manual p. 46), the tick, which INITMOUSE anchors about 125 cycles after the
// blanking edge it saw (bank 2 $235-$245 and bank 6 $600-$645): blanking begins
// at frame cycle 12,480 in this emulator, so the interrupt lands near 12,610.
// Bit 0 gates movement and button interrupts (Tech Note Mouse #3).
TEST_CASE(
    "Mouse card: movement and button interrupts land at the tick inside "
    "blanking, once per tick, and only while the mouse is on") {
  MouseSession_t session;
  session.align_to_video_frame();

  SUBCASE("mode $03: one move, one entry at the anchor") {
    CHECK_FALSE(session.run_calls_in_frames(
        {{entry_init_mouse, 0}, {entry_set_mouse, 0x03}}, 2));
    const uint32_t phase = video_frame_phase();
    CHECK(phase < 32);
    move_mouse(session.slot, 1, 0);
    poke_meter(session.slot);
    const std::vector<uint64_t> entries = session.run_metered_frames(1, 0);
    REQUIRE(entries.size() == 1);
    const uint64_t frame_cycle = (phase + entries.at(0)) % ntsc_frame;
    CAPTURE(frame_cycle);
    CHECK(frame_cycle >= 12590);
    CHECK(frame_cycle <= 12630);
    CHECK((meter_vbl(0) & 0x80) == 0);
    CHECK((mem[0x77C] & 0x0E) == 0x02);
  }

  SUBCASE("mode $03: two moves in one frame, one entry") {
    CHECK_FALSE(session.run_calls_in_frames(
        {{entry_init_mouse, 0}, {entry_set_mouse, 0x03}}, 2));
    move_mouse(session.slot, 1, 0);
    move_mouse(session.slot, 0, 1);
    poke_meter(session.slot);
    CHECK(session.run_metered_frames(1, 0).size() == 1);
  }

  SUBCASE("mode $05: a press and a release in one frame, one entry with $04") {
    CHECK_FALSE(session.run_calls_in_frames(
        {{entry_init_mouse, 0}, {entry_set_mouse, 0x05}}, 2));
    press_button(session.slot, true);
    press_button(session.slot, false);
    poke_meter(session.slot);
    CHECK(session.run_metered_frames(1, 0).size() == 1);
    CHECK((mem[0x77C] & 0x0E) == 0x04);
  }

  SUBCASE("mode $02: movement interrupts with the mouse off, nothing") {
    CHECK_FALSE(session.run_calls_in_frames(
        {{entry_init_mouse, 0}, {entry_set_mouse, 0x02}}, 2));
    move_mouse(session.slot, 1, 0);
    poke_meter(session.slot);
    CHECK(session.run_metered_frames(2, 0).empty());
  }

  SUBCASE("mode $01: tracking with no interrupt enabled, nothing") {
    CHECK_FALSE(session.run_calls_in_frames(
        {{entry_init_mouse, 0}, {entry_set_mouse, 0x01}}, 2));
    move_mouse(session.slot, 1, 0);
    press_button(session.slot, true);
    poke_meter(session.slot);
    CHECK(session.run_metered_frames(2, 0).empty());
  }
}

// The 6805's timer runs from Q3 through the PAL (schematic zones A3, C2), so
// the tick is a count of 6502 cycles on an NTSC and a PAL machine alike: 20,280
// once TIMEDATA selects 50 Hz "effective at the next INITMOUSE" (Tech Note
// Mouse #2). Anchored inside blanking, every entry reads $C019 below $80 (IIe
// Technical Reference p. 170).
TEST_CASE(
    "Mouse card: after INITMOUSE the refresh tick comes every 17,030 cycles "
    "inside blanking, one a frame on NTSC and 119 or 120 per 100 PAL frames, "
    "and 20,280 apart once TIMEDATA selects 50 Hz") {
  SUBCASE("NTSC, mode $09 and mode $08") {
    uint8_t mode = 0x09;
    SUBCASE("mode $09") { mode = 0x09; }
    SUBCASE("mode $08") { mode = 0x08; }
    MouseSession_t session;
    session.align_to_video_frame();
    CHECK_FALSE(session.run_calls_in_frames(
        {{entry_init_mouse, 0}, {entry_set_mouse, mode}}, 2));
    poke_meter(session.slot);
    const std::vector<uint64_t> entries = session.run_metered_frames(60, 0);
    CHECK(entries.size() == 60);
    const uint64_t handler = measured_handler_cycles(session, entries.size());
    CHECK(handler + 2 >= serve_handler_cycles);
    CHECK(handler <= serve_handler_cycles + 2);
    for (size_t i = 0; i < entries.size(); ++i) {
      CAPTURE(i);
      CHECK((meter_vbl(i) & 0x80) == 0);
      CHECK_FALSE(meter_serve_carry(i));
    }
    CHECK((mem[0x77C] & 0x0E) == 0x08);
    check_period(entries, ntsc_frame);
  }

  SUBCASE("PAL frames of 20,280 cycles") {
    MouseSession_t session(test_slot, true);
    REQUIRE(session.frame_length() == pal_frame);
    session.align_to_video_frame();
    CHECK_FALSE(session.run_calls_in_frames(
        {{entry_init_mouse, 0}, {entry_set_mouse, 0x09}}, 2));
    poke_meter(session.slot);
    std::vector<uint64_t> entries = session.run_metered_frames(100, 0);
    CHECK(entries.size() >= 119);
    CHECK(entries.size() <= 120);
    check_period(entries, ntsc_frame);

    CHECK_FALSE(session.run_calls_in_frames({{entry_time_data, 0x91},
                                             {entry_init_mouse, 0},
                                             {entry_set_mouse, 0x09}},
                                            2));
    poke_meter(session.slot);
    entries = session.run_metered_frames(20, 0);
    CHECK(entries.size() == 20);
    check_period(entries, pal_frame);
  }
}

// The card decodes nothing but DEVICE SELECT' (schematic zone C3), so the same
// firmware, registers and holes appear at any slot's addresses (manual p. 44).
TEST_CASE(
    "Mouse card: the card works the same in slots 1, 5 and 7, and two cards "
    "hold the interrupt line independently") {
  SUBCASE("one card in another slot") {
    int slot = 5;
    SUBCASE("slot 5") { slot = 5; }
    SUBCASE("slot 1") { slot = 1; }
    SUBCASE("slot 7") { slot = 7; }
    MouseMachine_t machine(slot);
    const auto page = static_cast<uint16_t>(0xC000 + (slot << 8));
    const auto base = static_cast<uint16_t>(0xC080 + (slot << 4));
    const std::vector<uint8_t> values =
        read_addresses(machine, {page, static_cast<uint16_t>(page + 1),
                                 static_cast<uint16_t>(page + 0xFB), base,
                                 static_cast<uint16_t>(base + 1),
                                 static_cast<uint16_t>(base + 2),
                                 static_cast<uint16_t>(base + 3)});
    CHECK(values.at(0) == 0x2C);
    CHECK(values.at(1) == 0x58);
    CHECK(values.at(2) == 0xD6);
    CHECK(values.at(3) == 0x00);
    CHECK(values.at(4) == 0x00);
    CHECK(values.at(5) == 0x00);
    CHECK(values.at(6) == 0x00);

    REQUIRE_FALSE(call_firmware(slot, entry_set_mouse, 0x01));
    CHECK(mem[0x7F8 + slot] == 0x01);
    move_mouse(slot, 7, 9);
    const Reading_t reading = read_mouse(slot);
    CHECK(reading.x == 7);
    CHECK(reading.y == 9);
    CHECK(reading.status == 0x20);
    CHECK(mem[0x478 + slot] == 7);
    CHECK(mem[0x4F8 + slot] == 9);
  }

  SUBCASE("two cards in slots 4 and 5") {
    Description_t description = describe_mouse_in(4);
    description.slots[4] = "Mouse Interface";
    MouseSession_t session(description, 4);
    REQUIRE_FALSE(call_firmware(4, entry_set_mouse, 0x08));
    REQUIRE_FALSE(call_firmware(5, entry_set_mouse, 0x08));

    SUBCASE("serving slot 4 alone leaves slot 5 holding the line") {
      poke_meter(4);
      session.run_metered_frames(1);
      CHECK(meter_entries().size() > 10);
    }
    SUBCASE("serving slot 5 alone leaves slot 4 holding the line") {
      poke_meter(5);
      session.run_metered_frames(1);
      CHECK(meter_entries().size() > 10);
    }
    SUBCASE("a handler that serves both releases the line once a frame") {
      poke_meter(4);
      std::vector<uint8_t> handler;
      emit_firmware_call(handler, 5, entry_serve_mouse, 0);
      handler.push_back(0x4C);
      handler.push_back(static_cast<uint8_t>(handler_start & 0xFF));
      handler.push_back(static_cast<uint8_t>(handler_start >> 8));
      const auto front = static_cast<uint16_t>(handler_start - handler.size());
      TestFixtures::ScopedCore_t::poke(front, handler.data(), handler.size());
      const std::array<uint8_t, 2> vector = {static_cast<uint8_t>(front & 0xFF),
                                             static_cast<uint8_t>(front >> 8)};
      TestFixtures::ScopedCore_t::poke(IRQ_VECTOR_ADDR, vector);
      const std::vector<uint64_t> entries = session.run_metered_frames(3);
      CHECK(entries.size() >= 3);
      CHECK(entries.size() <= 4);
      CHECK((mem[0x77C] & 0x0E) == 0x08);
      CHECK((mem[0x77D] & 0x0E) == 0x08);
    }
  }
}

// Every handshake loop in the ROM waits on PB6 or PB7 with no timeout (bank 3
// $30E-$33F writing, bank 6 $686-$6C4 reading), so a return within the cap
// proves the 6805 model answers every strobe. The cycle counts are regression
// pins of this emulator, not hardware facts.
TEST_CASE(
    "Mouse card: the write and read handshakes terminate, and SETMOUSE and "
    "READMOUSE cost the cycles they cost") {
  MouseMachine_t machine;
  const StepResult_t set = step_firmware(machine.slot, entry_set_mouse, 0x01);
  REQUIRE(set.completed);
  CHECK_FALSE(set.carry);
  CHECK(set.cycles == 250);
  const StepResult_t read = step_firmware(machine.slot, entry_read_mouse, 0);
  REQUIRE(read.completed);
  CHECK_FALSE(read.carry);
  CHECK(read.cycles == 630);
}

// GetClamp (Tech Note Mouse #7) peeks $4E down to $47 for MaxYL, MaxXL, MaxYH,
// MaxXH, MinYL, MinXL, MinYH, MinXH.
TEST_CASE(
    "Mouse card: the $F0 peeks answer GetClamp's eight bytes, and the $Cn1D "
    "entry's data byte and the $F1 poke's bytes keep the stream in step") {
  MouseMachine_t machine;
  poke_slot0_holes(0x64, 0xC8, 0x00, 0x00);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_clamp_mouse, 0));
  poke_slot0_holes(0x2C, 0x90, 0x01, 0x01);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_clamp_mouse, 1));

  const std::array<uint8_t, 8> expected = {0x90, 0xC8, 0x01, 0x00,
                                           0x2C, 0x64, 0x01, 0x00};
  for (size_t i = 0; i < expected.size(); ++i) {
    poke_slot0_holes(static_cast<uint8_t>(0x4E - i), 0x00, 0x00, 0x00);
    REQUIRE_FALSE(call_firmware(machine.slot, entry_peek_poke, 0));
    CHECK(mem[0x578] == expected.at(i));
  }

  REQUIRE_FALSE(call_firmware(machine.slot, entry_home_mouse, 0));
  REQUIRE_FALSE(call_firmware(machine.slot, entry_data_byte, 0x40));
  REQUIRE_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x01));
  move_mouse(machine.slot, 3, 4);
  Reading_t reading = read_mouse(machine.slot);
  CHECK(reading.x == 103);
  CHECK(reading.y == 304);

  poke_slot0_holes(0x10, 0x20, 0x30, 0x00);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_peek_poke, 1));
  move_mouse(machine.slot, 1, 1);
  reading = read_mouse(machine.slot);
  CHECK(reading.x == 104);
  CHECK(reading.y == 305);
}

// $80 "places the mouse in BASIC mode and sets the mouse position numbers to
// zero" (manual p. 35); off, motion is ignored (Tech Note Mouse #3). SETMOUSE
// changes no position (p. 47), so it can observe the off leg.
TEST_CASE(
    "Mouse card: $80 turns tracking on at (0, 0) and $00 turns it off, "
    "leaving the position") {
  MouseMachine_t machine;
  REQUIRE_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x01));
  poke_slot_holes(machine.slot, 300, 100);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_pos_mouse, 0));
  Reading_t reading = read_mouse(machine.slot);
  CHECK(reading.x == 300);
  CHECK(reading.y == 100);

  send_raw_byte(machine, 0x80);
  move_mouse(machine.slot, 37, 11);
  reading = read_mouse(machine.slot);
  CHECK(reading.x == 37);
  CHECK(reading.y == 11);
  CHECK(reading.status == 0x20);

  send_raw_byte(machine, 0x00);
  move_mouse(machine.slot, 5, 5);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x01));
  reading = read_mouse(machine.slot);
  CHECK(reading.x == 37);
  CHECK(reading.y == 11);
  CHECK(reading.status == 0x00);
}

// "Any mouse motion is ignored" while the mouse is off (Tech Note Mouse #3).
TEST_CASE(
    "Mouse card: motion while the mouse is off leaves the position and bit 5 "
    "untouched") {
  MouseMachine_t machine;
  REQUIRE_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x01));
  poke_slot_holes(machine.slot, 20, 30);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_pos_mouse, 0));
  REQUIRE(read_mouse(machine.slot).x == 20);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x00));
  move_mouse(machine.slot, 50, 50);
  REQUIRE_FALSE(call_firmware(machine.slot, entry_set_mouse, 0x01));
  const Reading_t reading = read_mouse(machine.slot);
  CHECK(reading.x == 20);
  CHECK(reading.y == 30);
  CHECK(reading.status == 0x00);
}

// RES' reaches the PIA and the 6805 (schematic P1-31): the PIA's registers
// read zero (MC6821 "Initialization"), the pull-downs select bank 0, the 6805's
// ports go to input (MC6805P 8.1) so IRQ' is released, and the subsystem comes
// up off at (0, 0) at 60 Hz (manual p. 44; Tech Note Mouse #2).
TEST_CASE(
    "Mouse card: reset returns the PIA to zero and the slot page to bank 0, "
    "drops a pending interrupt, and restarts the tick at 60 Hz") {
  SUBCASE("the registers and the page, stepped") {
    MouseMachine_t machine;
    const auto base = static_cast<uint16_t>(0xC080 + (machine.slot << 4));
    const auto page = static_cast<uint16_t>(0xC000 + (machine.slot << 8));
    select_bank_stepped(machine.slot, 3);
    std::vector<uint8_t> values =
        read_addresses(machine, {page, static_cast<uint16_t>(base + 2)});
    CHECK(values.at(0) == mouse_rom.at(3 * mouse_rom_bank_size));
    CHECK((values.at(1) & 0x0E) == 0x06);

    linapple_reset_hard();
    values =
        read_addresses(machine, {page, base, static_cast<uint16_t>(base + 1),
                                 static_cast<uint16_t>(base + 2),
                                 static_cast<uint16_t>(base + 3)});
    CHECK(values.at(0) == 0x2C);
    CHECK(values.at(1) == 0x00);
    CHECK(values.at(2) == 0x00);
    CHECK(values.at(3) == 0x00);
    CHECK(values.at(4) == 0x00);
  }

  SUBCASE("the line, the position and the rate, under the frame loop") {
    MouseSession_t session;
    session.align_to_video_frame();
    CHECK_FALSE(session.run_calls_in_frames({{entry_time_data, 0x91},
                                             {entry_init_mouse, 0},
                                             {entry_set_mouse, 0x09}},
                                            2));
    poke_meter(session.slot);
    check_period(session.run_metered_frames(4, 0), pal_frame);
    move_mouse(session.slot, 40, 40);

    // A masked frame leaves the tick's interrupt pending.
    const uint16_t spin = poke_calls(session.slot, {});
    TestFixtures::enter_at({spin, 0, 0, 0});
    session.run_frames(2);

    linapple_reset_hard();
    poke_meter(session.slot);
    CHECK(session.run_metered_frames(1, 0).empty());
    REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x01));
    const Reading_t reading = read_mouse(session.slot);
    CHECK(reading.x == 0);
    CHECK(reading.y == 0);
    REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x08));
    poke_meter(session.slot);
    check_period(session.run_metered_frames(4), ntsc_frame);
  }

  SUBCASE("shutdown with the line asserted releases it") {
    BenchHost_t bench;
    const Peripheral_t* desc = mouse_descriptor();
    void* card = desc->init(test_slot, bench.host());
    REQUIRE(card != nullptr);
    bench_send(bench, card, 0x08);
    bench.set_cycles(2 * ntsc_frame);
    desc->think(card, 0);
    REQUIRE(bench.irq_calls().size() == 1);
    CHECK(bench.irq_calls().at(0).slot == test_slot);
    CHECK(bench.irq_calls().at(0).level);
    desc->shutdown(card);
    REQUIRE(bench.irq_calls().size() == 2);
    CHECK(bench.irq_calls().at(1).slot == test_slot);
    CHECK_FALSE(bench.irq_calls().at(1).level);
  }
}

namespace {

using Frame_t = std::array<uint8_t, frame_size>;

constexpr size_t frame_tick_phase = 32;
constexpr size_t frame_rate = 64;
constexpr size_t frame_pending = 65;
constexpr size_t frame_irq = 66;
constexpr size_t frame_in_len = 68;
constexpr size_t frame_reply_pos = 69;
constexpr size_t frame_mode = 74;
constexpr size_t frame_status = 76;
constexpr size_t frame_button_at_last_read = 77;
constexpr size_t frame_button = 79;

static_assert(offsetof(MouseSaveState_t, tick_phase) == frame_tick_phase,
              "the tick phase travels at byte 32");
static_assert(offsetof(MouseSaveState_t, rate_50hz) == frame_rate,
              "the rate travels at byte 64");
static_assert(offsetof(MouseSaveState_t, pending) == frame_pending,
              "the pending sources travel at byte 65");
static_assert(offsetof(MouseSaveState_t, irq_asserted) == frame_irq,
              "the line travels at byte 66");
static_assert(offsetof(MouseSaveState_t, parser_in_len) == frame_in_len,
              "the reply length travels at byte 68");
static_assert(offsetof(MouseSaveState_t, parser_reply_pos) == frame_reply_pos,
              "the reply cursor travels at byte 69");
static_assert(offsetof(MouseSaveState_t, mode) == frame_mode,
              "the mode travels at byte 74");
static_assert(offsetof(MouseSaveState_t, status) == frame_status,
              "the status byte travels at byte 76");
static_assert(offsetof(MouseSaveState_t, button_at_last_read) ==
                  frame_button_at_last_read,
              "the last-read button travels at byte 77");
static_assert(offsetof(MouseSaveState_t, button) == frame_button,
              "the button travels at byte 79");
static_assert(offsetof(MouseSaveState_t, buffer) == 81,
              "the command buffer travels at byte 81");
static_assert(sizeof(MouseSaveState_t) == frame_size, "the frame is 92 bytes");

auto save_frame(int slot) -> Frame_t {
  Frame_t frame{};
  size_t size = frame.size();
  peripheral_save_state(slot, frame.data(), &size);
  REQUIRE(size == frame.size());
  return frame;
}

auto frame_word(const Frame_t& frame, size_t at) -> uint32_t {
  return static_cast<uint32_t>(frame.at(at)) |
         (static_cast<uint32_t>(frame.at(at + 1)) << 8) |
         (static_cast<uint32_t>(frame.at(at + 2)) << 16) |
         (static_cast<uint32_t>(frame.at(at + 3)) << 24);
}

auto set_frame_word(Frame_t& frame, size_t at, uint32_t value) -> void {
  frame.at(at) = static_cast<uint8_t>(value & 0xFF);
  frame.at(at + 1) = static_cast<uint8_t>((value >> 8) & 0xFF);
  frame.at(at + 2) = static_cast<uint8_t>((value >> 16) & 0xFF);
  frame.at(at + 3) = static_cast<uint8_t>((value >> 24) & 0xFF);
}

auto frame_coordinate(const Frame_t& frame, size_t at) -> int16_t {
  return static_cast<int16_t>(static_cast<uint16_t>(frame_word(frame, at)));
}

// Derived from the layout and the firmware's register writes, not read off the
// card; bytes 32-35, the cycles to the next tick, are filled in by the case.
// The PIA is as SETMOUSE's write handshake left it: DDRA $FF, CRA and CRB $04,
// ORA the mode byte. ORB is $40 because the last ORB write is the return stub's
// read-modify-write (bank 3 $370-$378: LDA $C082,Y / AND #$F1 / ORA / STA),
// which copies PB6 high and PB7 low from the pins after the 6805 dropped busy.
// Port A holds INITMOUSE's reply, the last byte presented. Status $2E is
// movement since the last reading plus the three sources the tick reported.
constexpr Frame_t frame_after_tick = {
    0x01, 0x00, 0x00, 0x00, 0x5C, 0x00, 0x00, 0x00, 0x15, 0x03, 0x00, 0x00,
    0x41, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x40, 0xFF, 0x3E,
    0x04, 0x04, 0xFF, 0x40, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x40, 0x0F, 0x00, 0x2E, 0x00, 0x00, 0x01, 0x00, 0x0F, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// The slot-4 trailer entry of tests/fixtures/mouse-slot4-0aad3663.aws, written
// by an earlier card after SETMOUSE $0B, a host position of (123, 456), the
// button pressed and one READMOUSE: bytes 32-39 are the host window's 1023 x
// 1023 range, byte 76 the whole status byte, bytes 64-71 the PIA's unconnected
// pins.
constexpr Frame_t legacy_frame = {
    0x01, 0x00, 0x00, 0x00, 0x5C, 0x00, 0x00, 0x00, 0x7B, 0x00, 0x00, 0x00,
    0xC8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0x03, 0x00, 0x00, 0xFF, 0x03, 0x00, 0x00,
    0xFF, 0x03, 0x00, 0x00, 0x7B, 0x00, 0x00, 0x00, 0xC8, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0xA0, 0x40, 0x00, 0x3E,
    0x04, 0x04, 0xA0, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x40, 0x0B, 0x00, 0x80, 0x01, 0x00, 0x01, 0x00, 0x10, 0x7B, 0x00,
    0xC8, 0x01, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00};

}  // namespace

TEST_CASE(
    "Mouse card: the frame after INITMOUSE, SETMOUSE $0F, a move, the button "
    "and one tick is the derived 92 bytes, with the cycles to the next tick "
    "at byte 32") {
  MouseSession_t session;
  session.align_to_video_frame();
  CHECK_FALSE(session.run_calls_in_frames(
      {{entry_init_mouse, 0}, {entry_set_mouse, 0x0F}}, 2));
  move_mouse(session.slot, 789, 321);
  press_button(session.slot, true);
  const uint16_t spin = poke_calls(session.slot, {});
  TestFixtures::enter_at({spin, 0, 0, 0});
  session.run_frames(1);

  const Frame_t saved = save_frame(session.slot);
  const uint32_t phase = frame_word(saved, frame_tick_phase);
  CHECK(phase >= 1);
  CHECK(phase <= ntsc_frame);
  Frame_t expected = frame_after_tick;
  set_frame_word(expected, frame_tick_phase, phase);
  for (size_t i = 0; i < frame_size; ++i) {
    CAPTURE(i);
    CHECK(saved.at(i) == expected.at(i));
  }

  // The line is up at the save, so the handler runs at the CLI; the next entry,
  // its loop time plus the first handler, is the tick the phase named.
  poke_meter(session.slot);
  const std::vector<uint64_t> entries = session.run_metered_frames(1, 0);
  REQUIRE(entries.size() >= 2);
  CHECK(entries.at(0) < 100);
  const uint64_t second = entries.at(1) + serve_handler_cycles;
  CHECK(second + 16 >= phase);
  CHECK(second <= phase + 32);
}

TEST_CASE(
    "Mouse card: the frame carries the 6805's state, the tick's phase and the "
    "line, and a reset card takes it all back") {
  MouseSession_t session;
  REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x0F));
  poke_slot0_holes(0x64, 0xC8, 0x00, 0x00);
  REQUIRE_FALSE(call_firmware(session.slot, entry_clamp_mouse, 0));
  move_mouse(session.slot, 150, 20);
  press_button(session.slot, true);
  session.run_frames(1);

  const Frame_t saved = save_frame(session.slot);
  CHECK(frame_word(saved, 8) == 150);
  CHECK(frame_word(saved, 12) == 20);
  CHECK(frame_word(saved, 16) == 100);
  CHECK(frame_word(saved, 20) == 200);
  CHECK(frame_word(saved, frame_tick_phase) > 0);
  CHECK(frame_word(saved, frame_tick_phase) <= ntsc_frame);
  CHECK(saved.at(frame_rate) == 0);
  CHECK(saved.at(frame_pending) == 0);
  CHECK(saved.at(frame_irq) == 1);
  CHECK(saved.at(frame_in_len) == 0);
  CHECK(saved.at(frame_reply_pos) == 0);
  CHECK(saved.at(frame_mode) == 0x0F);
  CHECK(saved.at(frame_status) == 0x2E);
  CHECK(saved.at(frame_button) == 1);
  for (size_t at : {36U, 67U, 70U, 71U, 75U, 78U, 80U, 89U, 90U, 91U}) {
    CHECK(saved.at(at) == 0);
  }

  linapple_reset_hard();
  REQUIRE(peripheral_load_state(session.slot, saved.data(), saved.size()) ==
          peripheral_ok);
  const Frame_t reloaded = save_frame(session.slot);
  CHECK(reloaded == saved);

  CHECK_FALSE(call_firmware(session.slot, entry_serve_mouse, 0));
  CHECK((mem[0x77C] & 0x0E) == 0x0E);

  // Loaded again, the handler is entered at the CLI, not at the tick.
  REQUIRE(peripheral_load_state(session.slot, saved.data(), saved.size()) ==
          peripheral_ok);
  poke_meter(session.slot);
  const std::vector<uint64_t> entries = session.run_metered_frames(1);
  REQUIRE(entries.size() >= 1);
  CHECK(entries.at(0) < 200);

  REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x0F));
  Reading_t reading = read_mouse(session.slot);
  CHECK(reading.x == 150);
  CHECK(reading.y == 20);
  CHECK(reading.status == 0xA0);
  move_mouse(session.slot, 1000, 0);
  CHECK(read_mouse(session.slot).x == 200);
}

TEST_CASE(
    "Mouse card: a frame written before the tick existed loads with the host "
    "width as the phase, its status bits dropped with the line, and the "
    "button from its own fields") {
  MouseSession_t session;
  REQUIRE(peripheral_load_state(session.slot, legacy_frame.data(),
                                legacy_frame.size()) == peripheral_ok);
  const Frame_t rewritten = save_frame(session.slot);
  CHECK(rewritten.at(frame_status) == 0x00);
  CHECK(rewritten.at(frame_irq) == 0);
  CHECK(rewritten.at(frame_pending) == 0);
  CHECK(rewritten.at(frame_rate) == 0);
  CHECK(frame_word(rewritten, frame_tick_phase) == 1023);
  CHECK(frame_word(rewritten, 16) == 0);
  CHECK(frame_word(rewritten, 20) == 1023);

  // Before the first tick can come there is nothing to serve.
  CHECK(call_firmware(session.slot, entry_serve_mouse, 0));

  REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x0B));
  const Reading_t reading = read_mouse(session.slot);
  CHECK(reading.x == 123);
  CHECK(reading.y == 456);
  CHECK(mem[0x47C] == 0x7B);
  CHECK(mem[0x57C] == 0x00);
  CHECK(mem[0x4FC] == 0xC8);
  CHECK(mem[0x5FC] == 0x01);
  CHECK(reading.status == 0xC0);

  // The first tick comes within the 1,023 cycles the width reads as, plus the
  // instruction in flight.
  REQUIRE(peripheral_load_state(session.slot, legacy_frame.data(),
                                legacy_frame.size()) == peripheral_ok);
  poke_meter(session.slot);
  const std::vector<uint64_t> entries = session.run_metered_frames(1);
  REQUIRE(entries.size() >= 1);
  CHECK(entries.at(0) <= 1023 + 16);
  CHECK((mem[0x77C] & 0x0E) == 0x08);

  SUBCASE("status $2E with the line released loads as bit 5 alone") {
    Frame_t frame = legacy_frame;
    frame.at(frame_status) = 0x2E;
    REQUIRE(peripheral_load_state(session.slot, frame.data(), frame.size()) ==
            peripheral_ok);
    CHECK(call_firmware(session.slot, entry_serve_mouse, 0));
    CHECK(save_frame(session.slot).at(frame_status) == 0x20);
    REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x0B));
    CHECK((read_mouse(session.slot).status & 0x20) == 0x20);
  }

  SUBCASE("a phase beyond one period is bounded by it") {
    Frame_t frame = legacy_frame;
    set_frame_word(frame, frame_tick_phase, 100000);
    REQUIRE(peripheral_load_state(session.slot, frame.data(), frame.size()) ==
            peripheral_ok);
    CHECK(frame_word(save_frame(session.slot), frame_tick_phase) == ntsc_frame);
    poke_meter(session.slot);
    const std::vector<uint64_t> bounded = session.run_metered_frames(2, 0);
    REQUIRE(bounded.size() >= 1);
    CHECK(bounded.at(0) <= ntsc_frame + 16);
  }

  // The hole's bits 7 and 6 come from the two button fields; a frame whose
  // status byte carries them with both fields zero reads a released button.
  SUBCASE("bits 7 and 6 of the status byte are never read from the frame") {
    Frame_t frame = legacy_frame;
    frame.at(frame_status) = 0xC0;
    frame.at(frame_button_at_last_read) = 0;
    frame.at(frame_button) = 0;
    REQUIRE(peripheral_load_state(session.slot, frame.data(), frame.size()) ==
            peripheral_ok);
    REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x0B));
    CHECK(read_mouse(session.slot).status == 0x00);
  }

  SUBCASE("the 104-byte fixed region of an older file loads") {
    std::array<uint8_t, 104> region{};
    std::copy(legacy_frame.begin(), legacy_frame.end(), region.begin());
    REQUIRE(peripheral_load_state(session.slot, region.data(), region.size()) ==
            peripheral_ok);
    CHECK(frame_word(save_frame(session.slot), 8) == 123);
  }
}

TEST_CASE(
    "Mouse card: a refused frame leaves every byte of the card's state as it "
    "was") {
  MouseSession_t session;
  REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x03));
  move_mouse(session.slot, 40, 50);
  const Frame_t before = save_frame(session.slot);

  auto refused = [&](Frame_t frame, size_t size) {
    CHECK(peripheral_load_state(session.slot, frame.data(), size) ==
          peripheral_error);
    CHECK(save_frame(session.slot) == before);
  };

  Frame_t frame = before;
  refused(frame, frame_size - 1);
  frame = before;
  frame.at(0) = 2;
  refused(frame, frame_size);
  frame = before;
  frame.at(4) = 50;
  refused(frame, frame_size);
  frame = before;
  set_frame_word(frame, 48, 8);
  refused(frame, frame_size);
  frame = before;
  set_frame_word(frame, 52, 9);
  refused(frame, frame_size);
  frame = before;
  frame.at(frame_pending) = 0x10;
  refused(frame, frame_size);
  frame = before;
  set_frame_word(frame, 20, 0x10000);
  refused(frame, frame_size);
  frame = before;
  frame.at(frame_in_len) = 3;
  refused(frame, frame_size);
  frame = before;
  frame.at(frame_reply_pos) = 1;
  refused(frame, frame_size);
  frame = before;
  frame.at(frame_mode) = 0x10;
  refused(frame, frame_size);
  frame = before;
  frame.at(frame_button) = 2;
  refused(frame, frame_size);
  CHECK(peripheral_load_state(session.slot, nullptr, frame_size) ==
        peripheral_error);
  CHECK(save_frame(session.slot) == before);
}

namespace {

// A command of up to five bytes may be in flight in a loaded frame, so up to
// four SETMOUSE bytes complete it before a fresh one is sure to be taken.
constexpr int stale_command_flush = 4;

}  // namespace

// Byte 73 is always refused: replies that do not answer their strobes would
// park the firmware. Byte 63 is the chip's copy of the shadow and never read. A
// taken frame may select any bank through ORB, so bank 0 is brought back before
// the entry table is read.
TEST_CASE(
    "Mouse card: each of the 92 frame bytes corrupted five ways is taken or "
    "refused, never anything else, and a taken frame leaves a working card") {
  MouseSession_t session;
  Frame_t base = frame_after_tick;
  set_frame_word(base, frame_tick_phase, 1000);
  REQUIRE(peripheral_load_state(session.slot, base.data(), base.size()) ==
          peripheral_ok);

  unsigned taken = 0;
  unsigned refused = 0;
  const std::array<uint8_t, 5> values = {0x00, 0x01, 0x7F, 0x80, 0xFF};
  for (size_t offset = 0; offset < frame_size; ++offset) {
    for (uint8_t value : values) {
      CAPTURE(offset);
      CAPTURE(value);
      Frame_t frame = base;
      frame.at(offset) = value;
      const PeripheralStatus_t status =
          peripheral_load_state(session.slot, frame.data(), frame.size());
      const bool answered =
          status == peripheral_ok || status == peripheral_error;
      CHECK(answered);
      if (offset == 73) {
        CHECK(status == peripheral_error);
      }
      if (offset == 63) {
        CHECK(status == peripheral_ok);
      }
      if (status != peripheral_ok) {
        ++refused;
        continue;
      }
      ++taken;
      select_bank_stepped(session.slot, 0);
      bool alive = true;
      for (int i = 0; i < stale_command_flush + 1 && alive; ++i) {
        alive = step_firmware(session.slot, entry_set_mouse, 0x01).completed;
      }
      if (alive) {
        move_mouse(session.slot, 1, 1);
        const StepResult_t read =
            step_firmware(session.slot, entry_read_mouse, 0);
        alive = read.completed;
        if (alive) {
          const int16_t x = word_at(0x47C, 0x57C);
          const int16_t y = word_at(0x4FC, 0x5FC);
          const int16_t min_x = frame_coordinate(frame, 16);
          const int16_t max_x = frame_coordinate(frame, 20);
          const int16_t min_y = frame_coordinate(frame, 24);
          const int16_t max_y = frame_coordinate(frame, 28);
          CHECK(x >= std::min(min_x, max_x));
          CHECK(x <= std::max(min_x, max_x));
          CHECK(y >= std::min(min_y, max_y));
          CHECK(y <= std::max(min_y, max_y));
          const StepResult_t next =
              step_firmware(session.slot, entry_set_mouse, 0x02);
          alive = next.completed && !next.carry;
        }
      }
      CHECK_MESSAGE(alive, "the firmware did not come back after the load");
      if (!alive) {
        linapple_reset_hard();
      }
    }
  }
  // 160 of the 460 corruptions hit a header, a word above 16 bits, the parser,
  // the mode, a flag, the reply fields or port B's replies.
  CHECK(taken == 300);
  CHECK(refused == 160);
}

// The session types at the Applesoft prompt, which needs the keyboard card.
#if defined(ENABLE_PERIPHERAL_KEYBOARD)
namespace {

constexpr uint32_t prompt_frame_cap = 300;

// With no disk controller the Autostart scan falls through to Applesoft.
struct BasicSession_t {
  MouseSession_t session;

  BasicSession_t() {
    uint32_t frames = 0;
    while (!screen_has_row("]") && frames < prompt_frame_cap) {
      session.run_frames(1);
      ++frames;
    }
    CAPTURE(frames);
    REQUIRE(screen_has_row("]"));
  }

  auto screen_has_row(const std::string& text) const -> bool {
    for (int row = 0; row < 24; ++row) {
      if (session.harness.get_text_row(row) == text) {
        return true;
      }
    }
    return false;
  }

  auto type_line(const std::string& text) -> void {
    session.harness.type_string(text + "\r", 2);
    session.run_frames(8);
  }

  // The Enhanced //e reaches its prompt with the internal $Cn00 ROM switched
  // in (IIe Technical Reference, SETSLOTCXROM at $C006).
  auto select_slot_roms() -> void {
    const std::array<uint8_t, 6> program = {0x8D, 0x06, 0xC0,   // STA $C006
                                            0x4C, 0x03, 0x03};  // JMP $0303
    TestFixtures::ScopedCore_t::poke(program_start, program);
    TestFixtures::enter_at({program_start, 0, 0, 0});
    TestFixtures::step_until_pc(program_start + 3, cycle_cap);
    REQUIRE(cpu_get_registers()->pc == program_start + 3);
  }
};

}  // namespace

// PR#n and IN#n: manual pp. 35-37; CHR$(1) sends $80 and CHR$(0) $00 (bank 4
// $411-$42E). PR#0 must follow on the same line because every byte printed
// afterwards would be a command by its low bit; Applesoft refuses INPUT outside
// a program, so that line runs under RUN. With tracking off the input path
// zeroes the holes and never asks the card (bank 4 $440-$452), so the card's
// own position is read through a stepped SETMOUSE and READMOUSE.
TEST_CASE(
    "Mouse card: PR#4 with CHR$(1) and IN#4 read the mouse from Applesoft, "
    "and CHR$(0) turns tracking off at the card") {
  BasicSession_t basic;
  basic.type_line("PR#4 : PRINT CHR$(1) : PR#0");
  move_mouse(test_slot, 37, 11);
  basic.type_line("10 IN#4 : INPUT X,Y,S : IN#0 : PRINT X;\",\";Y;\",\";S");
  basic.type_line("RUN");
  CHECK(basic.screen_has_row("37,11,4"));

  basic.type_line("PR#4 : PRINT CHR$(0) : PR#0");
  move_mouse(test_slot, 5, 5);
  basic.type_line("RUN");
  CHECK(basic.screen_has_row("0,0,1"));

  basic.select_slot_roms();
  REQUIRE_FALSE(call_firmware(test_slot, entry_set_mouse, 0x01));
  const Reading_t reading = read_mouse(test_slot);
  CHECK(reading.x == 37);
  CHECK(reading.y == 11);
}
#endif

namespace {

constexpr size_t frame_pia_orb = 57;
constexpr size_t frame_pia_ddrb = 59;
constexpr size_t frame_pia_port_b_in = 63;
constexpr size_t frame_port_b_shadow = 73;

struct LogLines_t {
  std::vector<std::string> lines;
};

auto collect_log_line(LogLevel level, const char* message, void* user_data)
    -> void {
  (void)level;
  auto* lines = static_cast<LogLines_t*>(user_data);
  if (lines != nullptr && message != nullptr) {
    lines->lines.emplace_back(message);
  }
}

class ScopedLogCapture_t {
 public:
  ScopedLogCapture_t() : verbosity_(Logger::get_verbosity()) {
    Logger::set_verbosity(LogLevel::info);
    Logger::set_callback_with_context(collect_log_line, &lines_);
  }
  ~ScopedLogCapture_t() {
    Logger::set_callback_with_context(nullptr, nullptr);
    Logger::set_verbosity(verbosity_);
  }
  ScopedLogCapture_t(const ScopedLogCapture_t&) = delete;
  auto operator=(const ScopedLogCapture_t&) -> ScopedLogCapture_t& = delete;
  ScopedLogCapture_t(ScopedLogCapture_t&&) = delete;
  auto operator=(ScopedLogCapture_t&&) -> ScopedLogCapture_t& = delete;

  auto count_containing(const std::string& needle) const -> size_t {
    size_t count = 0;
    for (const std::string& line : lines_.lines) {
      if (line.find(needle) != std::string::npos) {
        ++count;
      }
    }
    return count;
  }

 private:
  LogLevel verbosity_;
  LogLines_t lines_;
};

auto slot_page_first_byte(int slot) -> uint8_t {
  const uint16_t sentinel = poke_page_copier(slot);
  TestFixtures::enter_at({program_start, 0, 0, 0});
  TestFixtures::step_until_pc(sentinel, cycle_cap);
  REQUIRE(cpu_get_registers()->pc == sentinel);
  return mem[page_copy];
}

}  // namespace

// A shadow whose replies already answer the strobe the firmware is about to
// raise gives it no edge to wait for, so that frame is refused. The bank and
// strobe bits are the PIA's pins, so a shadow that disagrees with ORB & DDRB
// takes the pins.
TEST_CASE(
    "Mouse card: a frame whose port B replies do not answer its strobes is "
    "refused, one whose shadow disagrees with the PIA's pins takes the pins, "
    "and READMOUSE returns after either") {
  MouseSession_t session;
  REQUIRE_FALSE(call_firmware(session.slot, entry_set_mouse, 0x01));
  move_mouse(session.slot, 10, 20);
  const Frame_t before = save_frame(session.slot);
  REQUIRE(before.at(frame_pia_orb) == 0x40);
  REQUIRE(before.at(frame_pia_ddrb) == 0x3E);
  REQUIRE(before.at(frame_pia_port_b_in) == 0x40);
  REQUIRE(before.at(frame_port_b_shadow) == 0x40);

  ScopedLogCapture_t log;

  SUBCASE("replies that do not answer their strobes are refused") {
    const std::array<uint8_t, 7> shadows = {0x7F, 0xFF, 0x50, 0x20,
                                            0x80, 0x01, 0x00};
    size_t refusals = 0;
    for (uint8_t shadow : shadows) {
      Frame_t frame = before;
      frame.at(frame_port_b_shadow) = shadow;
      CHECK(peripheral_load_state(session.slot, frame.data(), frame.size()) ==
            peripheral_error);
      CHECK(save_frame(session.slot) == before);
      ++refusals;
      CHECK(log.count_containing("refused for port B's replies") == refusals);
    }
    CHECK(read_mouse(session.slot).x == 10);
  }

  SUBCASE("the levels a save mid-handshake holds load as they are") {
    const std::array<uint8_t, 2> shadows = {0x10, 0xE0};
    for (uint8_t shadow : shadows) {
      Frame_t frame = before;
      frame.at(frame_port_b_shadow) = shadow;
      frame.at(frame_pia_orb) = static_cast<uint8_t>(shadow & 0x3E);
      REQUIRE(peripheral_load_state(session.slot, frame.data(), frame.size()) ==
              peripheral_ok);
      CHECK(save_frame(session.slot).at(frame_port_b_shadow) == shadow);
    }
    CHECK(log.count_containing("disagrees with the PIA's pins") == 0);
  }

  SUBCASE("a shadow selecting bank 7 over pins at bank 0 shows bank 0") {
    Frame_t frame = before;
    frame.at(frame_port_b_shadow) = 0x4E;
    REQUIRE(peripheral_load_state(session.slot, frame.data(), frame.size()) ==
            peripheral_ok);
    CHECK(log.count_containing("shadow $4E disagrees with the PIA's pins; "
                               "loading $40") == 1);
    CHECK(save_frame(session.slot).at(frame_port_b_shadow) == 0x40);
    CHECK(slot_page_first_byte(session.slot) == mouse_rom.at(0));
    CHECK(read_mouse(session.slot).x == 10);
  }

  SUBCASE("pins at bank 7 under a shadow at bank 0 show bank 7") {
    Frame_t frame = before;
    frame.at(frame_pia_orb) = 0x4E;
    REQUIRE(peripheral_load_state(session.slot, frame.data(), frame.size()) ==
            peripheral_ok);
    CHECK(save_frame(session.slot).at(frame_port_b_shadow) == 0x4E);
    CHECK(slot_page_first_byte(session.slot) ==
          mouse_rom.at(7 * mouse_rom_bank_size));
  }

  SUBCASE("a DDRB of inputs releases the bank the shadow held") {
    Frame_t frame = before;
    frame.at(frame_pia_orb) = 0x4E;
    frame.at(frame_pia_ddrb) = 0x00;
    frame.at(frame_port_b_shadow) = 0x4E;
    REQUIRE(peripheral_load_state(session.slot, frame.data(), frame.size()) ==
            peripheral_ok);
    CHECK(save_frame(session.slot).at(frame_port_b_shadow) == 0x40);
    CHECK(slot_page_first_byte(session.slot) == mouse_rom.at(0));
    CHECK(read_mouse(session.slot).x == 10);
  }

  SUBCASE("a strobe the pins dropped drops, and its reply follows") {
    Frame_t frame = before;
    frame.at(frame_port_b_shadow) = 0x10;
    REQUIRE(peripheral_load_state(session.slot, frame.data(), frame.size()) ==
            peripheral_ok);
    CHECK(save_frame(session.slot).at(frame_port_b_shadow) == 0x40);
    CHECK(log.count_containing("disagrees with the PIA's pins") == 1);
    CHECK(read_mouse(session.slot).x == 10);
  }

  SUBCASE("byte 63 is not read: the chip's port B is the shadow") {
    Frame_t frame = before;
    frame.at(frame_pia_port_b_in) = 0x00;
    REQUIRE(peripheral_load_state(session.slot, frame.data(), frame.size()) ==
            peripheral_ok);
    CHECK(save_frame(session.slot).at(frame_pia_port_b_in) == 0x40);
    CHECK(read_mouse(session.slot).x == 10);
  }
}
