// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

extern "C" unsigned keyboard_abi_c_frame_size(void);
extern "C" unsigned keyboard_abi_c_state_version(void);
extern "C" unsigned keyboard_abi_c_repeat_key_offset(void);
extern "C" unsigned keyboard_abi_c_latch_offset(void);
extern "C" unsigned keyboard_abi_c_strobe_offset(void);
extern "C" unsigned keyboard_abi_c_caps_lock_offset(void);
extern "C" unsigned keyboard_abi_c_auto_repeat_offset(void);
extern "C" unsigned keyboard_abi_c_key_event_size(void);

namespace {

constexpr uint16_t addr_keyboard_data = 0xC000;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint8_t strobe_bit = 0x80;
constexpr size_t frame_size = 552;
constexpr uint32_t foreign_joystick_id = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0001;
constexpr uint32_t foreign_disk_id = PERIPHERAL_SUBSYSTEM_DISK | 0x0003;
constexpr uint32_t unknown_keyboard_id = PERIPHERAL_SUBSYSTEM_KEYBOARD | 0x00FF;
// A scanner byte with bit 7 set: what the bus holds while the text page is
// full of normal spaces.
constexpr uint8_t bus_marker = 0xA0;
constexpr uint16_t program_base = 0x0300;
constexpr uint32_t program_cycle_cap = 1000;

// Through the registry, so one binary covers the built-in card and a plugin.
auto keyboard_card() -> Peripheral_t* {
  Peripheral_t* descriptor = peripheral_find_internal("linapple.keyboard");
  REQUIRE(descriptor != nullptr);
  return descriptor;
}

struct BenchHandler_t {
  void* instance;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
};

// A host built by hand, for what the real one cannot show: the handlers a
// card registers and the status a call returns.
class BenchHost_t {
 public:
  BenchHost_t() {
    s_active = this;
    host_.Log = bench_log;
    host_.RegisterDirectIO = bench_register_direct_io;
    host_.ReadFloatingBus = bench_read_floating_bus;
    host_.GetCycles = bench_get_cycles;
    host_.GetClockHz = bench_get_clock_hz;
    host_.ScheduleEvent = bench_schedule_event;
    host_.GetMachine = bench_get_machine;
    host_.GetFrameCycles = bench_get_frame_cycles;
  }

  ~BenchHost_t() {
    if (instance_ != nullptr) {
      keyboard_card()->shutdown(instance_);
    }
    s_active = nullptr;
  }

  BenchHost_t(const BenchHost_t&) = delete;
  auto operator=(const BenchHost_t&) -> BenchHost_t& = delete;
  BenchHost_t(BenchHost_t&&) = delete;
  auto operator=(BenchHost_t&&) -> BenchHost_t& = delete;

  auto host() -> HostInterface_t* { return &host_; }

  auto create() -> void* {
    instance_ = keyboard_card()->init(0, &host_);
    return instance_;
  }

  auto read(uint16_t addr) -> uint8_t {
    auto it = handlers_.find(addr);
    REQUIRE(it != handlers_.end());
    REQUIRE(it->second.read != nullptr);
    return it->second.read(it->second.instance, 0, addr, 0, 0, 0);
  }

  auto command(uint32_t cmd_id, const void* data, size_t size)
      -> PeripheralStatus_t {
    return keyboard_card()->command(instance_, cmd_id, data, size);
  }

  auto press(uint32_t host_key, uint8_t code) -> PeripheralStatus_t {
    const KeyboardKeyEvent_t event{host_key, code, 1, {0, 0, 0, 0, 0, 0}};
    return command(keyboard_cmd_key, &event, sizeof(event));
  }

  auto release(uint32_t host_key) -> PeripheralStatus_t {
    const KeyboardKeyEvent_t event{host_key, 0, 0, {0, 0, 0, 0, 0, 0}};
    return command(keyboard_cmd_key, &event, sizeof(event));
  }

 private:
  HostInterface_t host_{};
  std::map<uint16_t, BenchHandler_t> handlers_;
  void* instance_ = nullptr;

  static BenchHost_t* s_active;

  static auto bench_log(void* instance, PeripheralLogLevel_t level,
                        const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    (void)fmt;
  }

  static auto bench_register_direct_io(void* instance, uint16_t addr,
                                       PeripheralIOHandler read,
                                       PeripheralIOHandler write) -> void {
    if (s_active != nullptr) {
      s_active->handlers_[addr] = BenchHandler_t{instance, read, write};
    }
  }

  static auto bench_read_floating_bus(uint32_t executed_cycles) -> uint8_t {
    (void)executed_cycles;
    return bus_marker;
  }

  static auto bench_get_cycles() -> uint64_t { return 0; }

  static auto bench_get_clock_hz() -> double { return 1020484.0; }

  static auto bench_schedule_event(void* instance, uint64_t at_cycle) -> void {
    (void)instance;
    (void)at_cycle;
  }

  static auto bench_get_machine() -> PeripheralMachine_t {
    return peripheral_machine_apple2e;
  }

  static auto bench_get_frame_cycles() -> uint32_t { return 17030; }
};

BenchHost_t* BenchHost_t::s_active = nullptr;

// The command queue is drained by a think, as a running machine drains it
// once a frame.
auto settle() -> void { peripheral_manager_think(0); }

// A program poked at $0300 and stepped to its final NOP, as the Monitor would
// run it; the byte after the program's last store is the sentinel.
template <size_t N>
auto run_program(const std::array<uint8_t, N>& program) -> void {
  TestFixtures::ScopedCore_t::poke(program_base, program);
  TestFixtures::enter_at({program_base, 0, 0, 0});
  const auto sentinel = static_cast<uint16_t>(program_base + N - 1);
  TestFixtures::step_until_pc(sentinel, program_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == sentinel);
}

}  // namespace

TEST_CASE(
    "Keyboard: the registry resolves the card by id and by name, and the "
    "descriptor is the motherboard's keyboard") {
  Peripheral_t* descriptor = keyboard_card();
  CHECK(peripheral_find_internal("Keyboard") == descriptor);

  CHECK(descriptor->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::strcmp(descriptor->id, "linapple.keyboard") == 0);
  CHECK(std::strcmp(descriptor->name, "Keyboard") == 0);
  CHECK(descriptor->compatible_slots == PERIPHERAL_MASK_INTERNAL);
  CHECK(descriptor->default_slot == 0);

  CHECK(descriptor->init != nullptr);
  CHECK(descriptor->reset != nullptr);
  CHECK(descriptor->shutdown != nullptr);
  CHECK(descriptor->think != nullptr);
  CHECK(descriptor->save_state != nullptr);
  CHECK(descriptor->load_state != nullptr);
  CHECK(descriptor->command != nullptr);
  CHECK(descriptor->query != nullptr);
}

TEST_CASE(
    "Keyboard: a null host gets no card and the entry points take a null "
    "instance") {
  Peripheral_t* descriptor = keyboard_card();
  CHECK(descriptor->init(0, nullptr) == nullptr);
  descriptor->reset(nullptr);
  descriptor->shutdown(nullptr);
  descriptor->think(nullptr, 1000);

  BenchHost_t bench;
  CHECK(bench.create() != nullptr);
}

TEST_CASE(
    "Keyboard: an id from another subsystem is incompatible for command and "
    "query, so a neighbour in the slot still gets asked") {
  BenchHost_t bench;
  void* card = bench.create();
  REQUIRE(card != nullptr);

  uint8_t byte = 1;
  CHECK(keyboard_card()->command(card, foreign_joystick_id, &byte,
                                 sizeof(byte)) == peripheral_incompatible);
  CHECK(keyboard_card()->command(card, foreign_disk_id, &byte, sizeof(byte)) ==
        peripheral_incompatible);

  size_t size = sizeof(byte);
  CHECK(keyboard_card()->query(card, foreign_joystick_id, &byte, &size) ==
        peripheral_incompatible);
  size = sizeof(byte);
  CHECK(keyboard_card()->query(card, foreign_disk_id, &byte, &size) ==
        peripheral_incompatible);
  size = sizeof(byte);
  CHECK(keyboard_card()->query(card, PERIPHERAL_QUERY_AUDIO_INFO, &byte,
                               &size) == peripheral_incompatible);
}

TEST_CASE(
    "Keyboard: an id inside the keyboard's own subsystem that the card does "
    "not know is incompatible too") {
  BenchHost_t bench;
  void* card = bench.create();
  REQUIRE(card != nullptr);

  uint8_t byte = 1;
  CHECK(bench.command(unknown_keyboard_id, &byte, sizeof(byte)) ==
        peripheral_incompatible);
  CHECK(bench.command(unknown_keyboard_id, nullptr, 0) ==
        peripheral_incompatible);
  size_t size = sizeof(byte);
  CHECK(keyboard_card()->query(card, unknown_keyboard_id, &byte, &size) ==
        peripheral_incompatible);
}

TEST_CASE(
    "Keyboard: the save-state sizing probe answers 552 bytes and the C99 view "
    "of the frame and of the key payload matches the C++ one") {
  BenchHost_t bench;
  void* card = bench.create();
  REQUIRE(card != nullptr);

  size_t size = 0;
  CHECK(keyboard_card()->save_state(card, nullptr, &size) == peripheral_ok);
  CHECK(size == frame_size);
  std::array<uint8_t, frame_size> scratch{};
  size = frame_size - 1;
  CHECK(keyboard_card()->save_state(card, scratch.data(), &size) ==
        peripheral_error);
  CHECK(keyboard_card()->save_state(card, scratch.data(), nullptr) ==
        peripheral_error);

  static_assert(sizeof(KeyboardSaveState_t) == frame_size,
                "the version-1 frame is 552 bytes");
  CHECK(keyboard_abi_c_frame_size() == frame_size);
  CHECK(keyboard_abi_c_state_version() == KEYBOARD_STATE_VERSION);
  CHECK(keyboard_abi_c_repeat_key_offset() == 12);
  CHECK(keyboard_abi_c_repeat_key_offset() ==
        offsetof(KeyboardSaveState_t, repeat_key));
  CHECK(keyboard_abi_c_latch_offset() == 24);
  CHECK(keyboard_abi_c_latch_offset() ==
        offsetof(KeyboardSaveState_t, current_latch));
  CHECK(keyboard_abi_c_strobe_offset() == 25);
  CHECK(keyboard_abi_c_strobe_offset() ==
        offsetof(KeyboardSaveState_t, strobe));
  CHECK(keyboard_abi_c_caps_lock_offset() == 31);
  CHECK(keyboard_abi_c_caps_lock_offset() ==
        offsetof(KeyboardSaveState_t, caps_lock));
  CHECK(keyboard_abi_c_auto_repeat_offset() == 35);
  CHECK(keyboard_abi_c_auto_repeat_offset() ==
        offsetof(KeyboardSaveState_t, auto_repeat_enabled));
  static_assert(sizeof(KeyboardKeyEvent_t) == 12,
                "the key payload is 12 bytes");
  CHECK(keyboard_abi_c_key_event_size() == sizeof(KeyboardKeyEvent_t));
}

TEST_CASE(
    "Keyboard: a seven-bit code sent down latches under the strobe at $C000, "
    "a code above 127 is refused and changes nothing, and so is a payload of "
    "the wrong size") {
  BenchHost_t bench;
  void* card = bench.create();
  REQUIRE(card != nullptr);
  CHECK((bench.read(addr_keyboard_data) & strobe_bit) == 0);

  REQUIRE(bench.press(4, 'A') == peripheral_ok);
  CHECK(bench.read(addr_keyboard_data) == ('A' | strobe_bit));

  CHECK(bench.press(5, 0x80) == peripheral_error);
  CHECK(bench.read(addr_keyboard_data) == ('A' | strobe_bit));

  const KeyboardKeyEvent_t event{6, 'B', 1, {0, 0, 0, 0, 0, 0}};
  CHECK(bench.command(keyboard_cmd_key, &event, sizeof(event) - 1) ==
        peripheral_error);
  std::array<uint8_t, sizeof(event) + 1> long_event{};
  std::memcpy(long_event.data(), &event, sizeof(event));
  CHECK(bench.command(keyboard_cmd_key, long_event.data(), long_event.size()) ==
        peripheral_error);
  CHECK(bench.command(keyboard_cmd_key, nullptr, sizeof(event)) ==
        peripheral_error);
  CHECK(bench.read(addr_keyboard_data) == ('A' | strobe_bit));

  // The REPT level is a byte; everything else is refused.
  const uint8_t level = 1;
  CHECK(bench.command(keyboard_cmd_rept, &level, sizeof(level)) ==
        peripheral_ok);
  CHECK(bench.command(keyboard_cmd_rept, &level, 0) == peripheral_error);
  CHECK(bench.command(keyboard_cmd_rept, long_event.data(), 2) ==
        peripheral_error);
}

TEST_CASE(
    "Keyboard: any-key-down at $C010 follows the keys the host holds and "
    "release_all, which takes no payload, lets go of every one") {
  BenchHost_t bench;
  void* card = bench.create();
  REQUIRE(card != nullptr);
  CHECK((bench.read(addr_keyboard_strobe) & strobe_bit) == 0);

  REQUIRE(bench.press(4, 'A') == peripheral_ok);
  CHECK((bench.read(addr_keyboard_strobe) & strobe_bit) != 0);
  REQUIRE(bench.press(5, 'B') == peripheral_ok);
  REQUIRE(bench.release(4) == peripheral_ok);
  CHECK((bench.read(addr_keyboard_strobe) & strobe_bit) != 0);

  const uint8_t byte = 0;
  CHECK(bench.command(keyboard_cmd_release_all, &byte, sizeof(byte)) ==
        peripheral_error);
  CHECK((bench.read(addr_keyboard_strobe) & strobe_bit) != 0);
  CHECK(bench.command(keyboard_cmd_release_all, nullptr, 0) == peripheral_ok);
  CHECK((bench.read(addr_keyboard_strobe) & strobe_bit) == 0);
  // The latch keeps the last code; only the strobe and the held keys go.
  CHECK((bench.read(addr_keyboard_data) & 0x7F) == 'B');
}

TEST_CASE(
    "Keyboard: through the bridge, the 6502 reads the code under the strobe "
    "at $C000 until an access to $C010-$C01F clears the strobe and leaves "
    "the code") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  linapple_set_key('a', 0x61, true);
  settle();

  // LDA $C000 / STA $10 / LDA $C000 / STA $11 / BIT $C010 / LDA $C000 /
  // STA $12 / NOP: KEYIN's own pattern, the strobe cleared only by the BIT
  // (Apple IIe Technical Reference Manual, p. 13; Sather, Understanding the
  // Apple IIe, 7-4).
  const std::array<uint8_t, 19> read_twice_then_clear = {
      0xAD, 0x00, 0xC0, 0x85, 0x10, 0xAD, 0x00, 0xC0, 0x85, 0x11,
      0x2C, 0x10, 0xC0, 0xAD, 0x00, 0xC0, 0x85, 0x12, 0xEA};
  run_program(read_twice_then_clear);
  CHECK(*mem_get_main_ptr(0x10) == 0xE1);
  CHECK(*mem_get_main_ptr(0x11) == 0xE1);
  CHECK(*mem_get_main_ptr(0x12) == 0x61);

  // STA $C010, STA $C01F and LDA $C010 each clear it as well.
  const std::array<std::array<uint8_t, 3>, 3> clearers = {{
      {0x8D, 0x10, 0xC0},
      {0x8D, 0x1F, 0xC0},
      {0xAD, 0x10, 0xC0},
  }};
  for (const auto& clearer : clearers) {
    CAPTURE(static_cast<int>(clearer[0]));
    CAPTURE(static_cast<int>(clearer[1]));
    linapple_set_key('a', 0x61, false);
    linapple_set_key('a', 0x61, true);
    settle();
    // LDA $C000 / STA $13 / <clearer> / LDA $C000 / STA $14 / NOP
    const std::array<uint8_t, 14> program = {
        0xAD,       0x00, 0xC0, 0x85, 0x13, clearer[0], clearer[1],
        clearer[2], 0xAD, 0x00, 0xC0, 0x85, 0x14,       0xEA};
    run_program(program);
    CHECK(*mem_get_main_ptr(0x13) == 0xE1);
    CHECK(*mem_get_main_ptr(0x14) == 0x61);
  }
}
