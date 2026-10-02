// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

extern "C" unsigned superserial_abi_c_frame_size(void);
extern "C" unsigned superserial_abi_c_state_version(void);

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
