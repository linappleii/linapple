// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
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
