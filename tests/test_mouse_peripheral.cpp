// SPDX-License-Identifier: GPL-2.0-only
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
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "apple2/peripherals/mouse/MouseRom.h"
#include "core/LinAppleCore.h"
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

namespace {

// Through the registry, so one binary covers the built-in card and a loaded
// plugin alike.
auto mouse_descriptor() -> Peripheral_t* {
  return peripheral_find_internal("linapple.mouse");
}

constexpr int test_slot = 4;
constexpr size_t frame_size = 92;
constexpr uint32_t foreign_command = 0x9999;
constexpr uint32_t unknown_mouse_id = PERIPHERAL_SUBSYSTEM_MOUSE | 0x7FFF;

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
    s_last_log.clear();
  }

  auto host() -> HostInterface_t* { return &host_; }
  static auto last_log() -> const std::string& { return s_last_log; }

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
  static auto bench_schedule_event(void* instance, uint64_t at_cycle) -> void {
    (void)instance;
    (void)at_cycle;
  }

  static std::string s_last_log;
  HostInterface_t host_{};
};

std::string BenchHost_t::s_last_log;

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

 private:
  BenchHost_t host_;
  void* instance_;
};

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

// ScopedTestConfig_t writes every slot, so a conf with no entry for one is
// made by taking the line out again.
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
    CHECK(BenchHost_t::last_log().find(member) != std::string::npos);
    CHECK(BenchHost_t::last_log().find("slot " + std::to_string(slot)) !=
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
    "inside it, is incompatible") {
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
    "Mouse card: the is-active query's sizing probe and the button payload "
    "agree with the C99 view") {
  BenchCard_t card;
  REQUIRE(card.instance() != nullptr);
  const Peripheral_t* desc = mouse_descriptor();

  CHECK(mouse_abi_c_is_active_query_id() == mouse_query_is_active);
  CHECK(mouse_abi_c_set_button_id() == mouse_cmd_set_button);
  CHECK(mouse_abi_c_button_payload_size() == sizeof(MouseButtonPayload_t));
  CHECK(mouse_abi_c_move_id() == mouse_cmd_move);
  CHECK(mouse_abi_c_move_payload_size() == sizeof(MouseMovePayload_t));

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

// An Enhanced //e with the card in one slot, built and reset as the frontends
// build it.
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

// Copies $Cn00-$CnFF to $2000.
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

// The signature bytes and the identification byte (AppleMouse II User's
// Manual p. 43; Apple II Technical Note Mouse #5), then the whole page.
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

namespace {

constexpr uint16_t indirect_jump = 0x0320;
constexpr uint16_t meter_loop = 0x0330;
constexpr uint16_t handler_start = 0x0380;
constexpr uint16_t meter_low_table = 0x2000;
constexpr uint16_t meter_high_table = 0x2100;
constexpr uint8_t meter_low = 0x06;
constexpr uint8_t meter_high = 0x09;
constexpr uint8_t meter_index = 0x0A;
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

// The table at $Cn12 holds the low bytes of the entries, so every call goes
// through it indirectly: LDA $Cn12+k / STA $07 / LDA #$Cn / STA $08 / LDA #a
// / LDX #$Cn / LDY #$n0 / JSR $0320, with JMP ($0007) at $0320.
auto emit_firmware_call(std::vector<uint8_t>& program, int slot, int entry,
                        uint8_t a) -> void {
  const auto page = static_cast<uint8_t>(0xC0 + slot);
  const auto table = static_cast<uint16_t>((page << 8) + 0x12 + entry);
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
                                     page,
                                     0xA0,
                                     static_cast<uint8_t>(slot << 4),
                                     0x20,
                                     static_cast<uint8_t>(indirect_jump & 0xFF),
                                     static_cast<uint8_t>(indirect_jump >> 8)};
  program.insert(program.end(), call.begin(), call.end());
}

auto poke_indirect_jump() -> void {
  const std::array<uint8_t, 3> jump = {0x6C, 0x07, 0x00};
  TestFixtures::ScopedCore_t::poke(indirect_jump, jump);
}

// Runs one firmware entry stepped, from $0300 to a spin, and returns the
// carry it came back with.
auto call_firmware(int slot, int entry, uint8_t a) -> bool {
  std::vector<uint8_t> program;
  emit_firmware_call(program, slot, entry, a);
  const auto spin = static_cast<uint16_t>(program_start + program.size());
  program.push_back(0x4C);
  program.push_back(static_cast<uint8_t>(spin & 0xFF));
  program.push_back(static_cast<uint8_t>(spin >> 8));
  TestFixtures::ScopedCore_t::poke(program_start, program.data(),
                                   program.size());
  poke_indirect_jump();
  TestFixtures::enter_at({program_start, 0, 0, 0});
  TestFixtures::step_until_pc(spin, firmware_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == spin);
  return (cpu_get_registers()->ps & 0x01) != 0;
}

// The main loop counts turns of 8 cycles in $06 and $09 (15 at a wrap of
// $06); the handler files the count in a table, then calls SERVEMOUSE through
// the table so the line is released, and returns.
auto poke_meter(int slot) -> void {
  const std::vector<uint8_t> loop = {
      0x58,                   // CLI
      0xE6, meter_low,        // INC $06
      0xD0, 0xFC,             // BNE $0331
      0xE6, meter_high,       // INC $09
      0x4C, 0x31,       0x03  // JMP $0331
  };
  TestFixtures::ScopedCore_t::poke(meter_loop, loop.data(), loop.size());

  std::vector<uint8_t> handler = {
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
      0xE6,
      meter_index  // INC $0A
  };
  emit_firmware_call(handler, slot, entry_serve_mouse, 0);
  handler.push_back(0x40);  // RTI
  TestFixtures::ScopedCore_t::poke(handler_start, handler.data(),
                                   handler.size());
  poke_indirect_jump();

  const std::array<uint8_t, 2> vector = {
      static_cast<uint8_t>(handler_start & 0xFF),
      static_cast<uint8_t>(handler_start >> 8)};
  TestFixtures::ScopedCore_t::poke(IRQ_VECTOR_ADDR, vector);

  const std::array<uint8_t, 1> zero = {0};
  TestFixtures::ScopedCore_t::poke(meter_low, zero);
  TestFixtures::ScopedCore_t::poke(meter_high, zero);
  TestFixtures::ScopedCore_t::poke(meter_index, zero);
  std::vector<uint8_t> blank(512, 0);
  TestFixtures::ScopedCore_t::poke(meter_low_table, blank.data(), blank.size());
}

// 8 cycles a turn, 7 more at every wrap of the low byte; the count itself
// wraps after 65,536 turns, so two entries are compared modulo that.
constexpr uint64_t meter_wrap = 8 * 65536 + 7 * 256;

auto meter_delta(uint64_t from, uint64_t to) -> uint64_t {
  return (to + meter_wrap - from) % meter_wrap;
}

auto meter_entries() -> std::vector<uint64_t> {
  std::vector<uint64_t> cycles;
  for (size_t i = 0; i < mem[meter_index]; ++i) {
    const uint64_t turns =
        mem[meter_low_table + i] +
        (static_cast<uint64_t>(mem[meter_high_table + i]) << 8);
    cycles.push_back(8 * turns + 7 * (turns >> 8));
  }
  return cycles;
}

// The frame loop the frontends drive, with the card in one slot.
struct MouseSession_t {
  TestFixtures::ScopedTestConfig_t config;
  HeadlessHarness_t harness;
  int slot;

  explicit MouseSession_t(int in_slot = test_slot)
      : config(describe_mouse_in(in_slot)), harness(config), slot(in_slot) {
    harness.boot();
  }

  // A partial frame first: the tick stands one frame after reset, and the
  // few hundred cycles the stepped calls took would otherwise leave it so
  // close to a frame's end that the handler straddles the boundary.
  auto run_metered_frames(uint32_t frames) -> std::vector<uint64_t> {
    TestFixtures::enter_at({meter_loop, 0, 0, 0});
    linapple_run_frame(lead_in_cycles);
    harness.run_frames(frames);
    return meter_entries();
  }
};

auto press_button(int slot, bool down) -> void {
  MouseButtonPayload_t payload{0, static_cast<uint8_t>(down ? 1 : 0), {0, 0}};
  REQUIRE(peripheral_command(slot, mouse_cmd_set_button, &payload,
                             sizeof(payload)) == peripheral_ok);
  peripheral_manager_think(0);
}

}  // namespace

// The tick free-runs from reset at one frame's length; a handler installed
// after SETMOUSE sees one entry a frame, each late in the frame rather than
// at its boundary, and the second SERVEMOUSE finds the event consumed.
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
  const uint64_t period = meter_delta(entries.at(0), entries.at(1));
  for (size_t i = 2; i < entries.size(); ++i) {
    const uint64_t delta = meter_delta(entries.at(i - 1), entries.at(i));
    CHECK(delta + 16 >= period);
    CHECK(delta <= period + 16);
  }
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

namespace {

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

// CLAMPMOUSE and the peek take their bytes from the slot-0 holes (manual
// p. 48; Tech Note Mouse #7).
auto poke_slot0_holes(uint8_t at_478, uint8_t at_4f8, uint8_t at_578,
                      uint8_t at_5f8) -> void {
  const std::array<uint8_t, 1> a = {at_478};
  const std::array<uint8_t, 1> b = {at_4f8};
  const std::array<uint8_t, 1> c = {at_578};
  const std::array<uint8_t, 1> d = {at_5f8};
  TestFixtures::ScopedCore_t::poke(0x478, a);
  TestFixtures::ScopedCore_t::poke(0x4F8, b);
  TestFixtures::ScopedCore_t::poke(0x578, c);
  TestFixtures::ScopedCore_t::poke(0x5F8, d);
}

auto poke_slot_holes(int slot, int16_t x, int16_t y) -> void {
  const auto n = static_cast<uint16_t>(slot);
  const std::array<uint8_t, 1> xl = {static_cast<uint8_t>(x & 0xFF)};
  const std::array<uint8_t, 1> xh = {static_cast<uint8_t>(x >> 8)};
  const std::array<uint8_t, 1> yl = {static_cast<uint8_t>(y & 0xFF)};
  const std::array<uint8_t, 1> yh = {static_cast<uint8_t>(y >> 8)};
  TestFixtures::ScopedCore_t::poke(0x478 + n, xl);
  TestFixtures::ScopedCore_t::poke(0x578 + n, xh);
  TestFixtures::ScopedCore_t::poke(0x4F8 + n, yl);
  TestFixtures::ScopedCore_t::poke(0x5F8 + n, yh);
}

auto move_mouse(int slot, int32_t dx, int32_t dy) -> void {
  MouseMovePayload_t payload{dx, dy};
  REQUIRE(peripheral_command(slot, mouse_cmd_move, &payload, sizeof(payload)) ==
          peripheral_ok);
  peripheral_manager_think(0);
}

// The firmware's own write handshake (bank 3 $C40E-$C43F), driven from a
// program so a byte reaches the 6805 with no firmware entry in between.
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
  const uint16_t control_a = static_cast<uint16_t>(base + 1);
  const uint16_t port_b = static_cast<uint16_t>(base + 2);
  const uint16_t control_b = static_cast<uint16_t>(base + 3);
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

}  // namespace

TEST_CASE(
    "Mouse card: CLAMPMOUSE takes its bytes in the firmware's order, as signed "
    "values, moves nothing, and HOMEMOUSE goes to the lower boundaries") {
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
}

// GetClamp (Tech Note Mouse #7) peeks $4E down to $47 and gets MaxYL, MaxXL,
// MaxYH, MaxXH, MinYL, MinXL, MinYH, MinXH; the $Cn1D entry's data byte must
// not be taken for a command.
TEST_CASE(
    "Mouse card: the $F0 peeks answer GetClamp's eight bytes and the $Cn1D "
    "entry's data byte keeps the stream in step") {
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
  const Reading_t reading = read_mouse(machine.slot);
  CHECK(reading.x == 103);
  CHECK(reading.y == 304);
}

// CHR$(1) after PR#n sends $80, "places the mouse in BASIC mode and sets the
// mouse position numbers to zero" (manual p. 35); CHR$(0) sends $00 and turns
// it off, after which motion is ignored (Tech Note Mouse #3). SETMOUSE
// changes no position (p. 47), so it is the stepped observer of the off leg.
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

// RES' reaches the PIA and the 6805 (schematic P1-31): the PIA's registers
// read zero (MC6821 "Initialization"), the pull-downs select bank 0.
TEST_CASE(
    "Mouse card: reset returns the PIA to zero and the slot page to bank 0") {
  MouseMachine_t machine;
  const auto base = static_cast<uint16_t>(0xC080 + (machine.slot << 4));
  const auto page = static_cast<uint16_t>(0xC000 + (machine.slot << 8));

  // The firmware's own bank switch to bank 3: DDRB $3E, then ORB $06.
  std::vector<uint8_t> program = {0xAD,
                                  static_cast<uint8_t>((base + 3) & 0xFF),
                                  static_cast<uint8_t>((base + 3) >> 8),
                                  0x29,
                                  0xFB,
                                  0x8D,
                                  static_cast<uint8_t>((base + 3) & 0xFF),
                                  static_cast<uint8_t>((base + 3) >> 8),
                                  0xA9,
                                  0x3E,
                                  0x8D,
                                  static_cast<uint8_t>((base + 2) & 0xFF),
                                  static_cast<uint8_t>((base + 2) >> 8),
                                  0xAD,
                                  static_cast<uint8_t>((base + 3) & 0xFF),
                                  static_cast<uint8_t>((base + 3) >> 8),
                                  0x09,
                                  0x04,
                                  0x8D,
                                  static_cast<uint8_t>((base + 3) & 0xFF),
                                  static_cast<uint8_t>((base + 3) >> 8),
                                  0xA9,
                                  0x06,
                                  0x8D,
                                  static_cast<uint8_t>((base + 2) & 0xFF),
                                  static_cast<uint8_t>((base + 2) >> 8)};
  const auto spin = static_cast<uint16_t>(program_start + program.size());
  program.push_back(0x4C);
  program.push_back(static_cast<uint8_t>(spin & 0xFF));
  program.push_back(static_cast<uint8_t>(spin >> 8));
  TestFixtures::ScopedCore_t::poke(program_start, program.data(),
                                   program.size());
  machine.run_until(program_start, spin);

  std::vector<uint8_t> values =
      read_addresses(machine, {page, static_cast<uint16_t>(base + 2)});
  CHECK(values.at(0) == mouse_rom.at(3 * mouse_rom_bank_size));
  CHECK((values.at(1) & 0x0E) == 0x06);

  linapple_reset_hard();
  values = read_addresses(machine, {page, base, static_cast<uint16_t>(base + 1),
                                    static_cast<uint16_t>(base + 2),
                                    static_cast<uint16_t>(base + 3)});
  CHECK(values.at(0) == 0x2C);
  CHECK(values.at(1) == 0x00);
  CHECK(values.at(2) == 0x00);
  CHECK(values.at(3) == 0x00);
  CHECK(values.at(4) == 0x00);
}
