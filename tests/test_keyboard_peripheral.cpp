// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "HeadlessHarness.h"
#include "apple2/Apple2Types.h"
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
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/SaveStateManager.h"
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

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr uint16_t addr_keyboard_data = 0xC000;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint16_t addr_keyboard_strobe_last = 0xC01F;
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
constexpr uint32_t ntsc_frame_cycles = 17030;
constexpr uint64_t repeat_period_cycles = 4 * ntsc_frame_cycles;
// A strobe is seen within one observer iteration of its wake, and the wake
// within one instruction of the armed cycle.
constexpr int64_t observer_tolerance = 45;

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
// card registers, the status a call returns and the line it logs.
class BenchHost_t {
 public:
  explicit BenchHost_t(
      PeripheralMachine_t machine = peripheral_machine_apple2e) {
    s_active = this;
    s_machine = machine;
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

  auto has_read_handler(uint16_t addr) -> bool {
    auto it = handlers_.find(addr);
    return it != handlers_.end() && it->second.read != nullptr;
  }

  auto has_write_handler(uint16_t addr) -> bool {
    auto it = handlers_.find(addr);
    return it != handlers_.end() && it->second.write != nullptr;
  }

  auto read(uint16_t addr) -> uint8_t {
    auto it = handlers_.find(addr);
    REQUIRE(it != handlers_.end());
    REQUIRE(it->second.read != nullptr);
    return it->second.read(it->second.instance, 0, addr, 0, 0, 0);
  }

  auto write(uint16_t addr) -> void {
    auto it = handlers_.find(addr);
    REQUIRE(it != handlers_.end());
    REQUIRE(it->second.write != nullptr);
    it->second.write(it->second.instance, 0, addr, 1, 0, 0);
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

  auto rept(bool down) -> PeripheralStatus_t {
    const uint8_t level = down ? 1 : 0;
    return command(keyboard_cmd_rept, &level, sizeof(level));
  }

  auto last_log() const -> const std::string& { return last_log_; }

 private:
  HostInterface_t host_{};
  std::map<uint16_t, BenchHandler_t> handlers_;
  void* instance_ = nullptr;
  std::string last_log_;

  static BenchHost_t* s_active;
  static PeripheralMachine_t s_machine;

  static auto bench_log(void* instance, PeripheralLogLevel_t level,
                        const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    if (s_active == nullptr || fmt == nullptr) {
      return;
    }
    std::array<char, 256> line{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(line.data(), line.size(), fmt, args);
    va_end(args);
    s_active->last_log_ = line.data();
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

  static auto bench_get_machine() -> PeripheralMachine_t { return s_machine; }

  static auto bench_get_frame_cycles() -> uint32_t { return ntsc_frame_cycles; }
};

BenchHost_t* BenchHost_t::s_active = nullptr;
PeripheralMachine_t BenchHost_t::s_machine = peripheral_machine_apple2e;

// The command queue is drained by a think, as a running machine drains it
// once a frame.
auto settle() -> void { peripheral_manager_think(0); }

auto press(uint32_t host_key, uint8_t code) -> void {
  linapple_set_key(host_key, code, true);
  settle();
}

auto release(uint32_t host_key) -> void {
  linapple_set_key(host_key, 0, false);
  settle();
}

auto any_key_down() -> bool {
  return (io_map_dispatch(0, addr_keyboard_strobe, 0, 0, 0) & strobe_bit) != 0;
}

auto keyboard_data() -> uint8_t {
  return io_map_dispatch(0, addr_keyboard_data, 0, 0, 0);
}

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

// A 6502 loop at $0300 that spins on $C000 bit 7, clears each strobe it sees
// through $C010, and records the 24-bit iteration count it saw it at in a
// table at $0380 (low byte), $03C0 (middle) and $0340 (high); the count runs
// in $08-$0A and the number of strobes seen in $06. The idle iteration is 15
// cycles, so each strobe's cycle is known to one iteration, which tells
// 68,120 from 68,000 inside one interval. An iteration whose count carries
// into the middle byte pays 7 cycles more, one that carries into the high
// byte 11, and each recorded strobe 38, which the arithmetic puts back.
struct StrobeObserver_t {
  static constexpr uint16_t table_low = 0x0380;
  static constexpr uint16_t table_middle = 0x03C0;
  static constexpr uint16_t table_high = 0x0340;
  static constexpr uint16_t strobe_count = 0x0006;
  static constexpr uint64_t idle_iteration_cycles = 15;
  static constexpr uint64_t byte_carry_cycles = 7;
  static constexpr uint64_t word_carry_cycles = 4;
  static constexpr uint64_t record_cycles = 38;
  static constexpr uint64_t read_offset_cycles = 12;
  uint64_t start_cycle = 0;

  // INC $08 / BNE +6 / INC $09 / BNE +2 / INC $0A / LDA $C000 / BPL $0300 /
  // BIT $C010 / LDX $06 / LDA $08 / STA $0380,X / LDA $09 / STA $03C0,X /
  // LDA $0A / STA $0340,X / INX / STX $06 / JMP $0300
  static auto install() -> void {
    const std::array<uint8_t, 41> loop = {
        0xE6, 0x08, 0xD0, 0x06, 0xE6, 0x09, 0xD0, 0x02, 0xE6, 0x0A, 0xAD,
        0x00, 0xC0, 0x10, 0xF1, 0x2C, 0x10, 0xC0, 0xA6, 0x06, 0xA5, 0x08,
        0x9D, 0x80, 0x03, 0xA5, 0x09, 0x9D, 0xC0, 0x03, 0xA5, 0x0A, 0x9D,
        0x40, 0x03, 0xE8, 0x86, 0x06, 0x4C, 0x00, 0x03};
    TestFixtures::ScopedCore_t::poke(program_base, loop);
    const std::array<uint8_t, 5> zero_page{};
    TestFixtures::ScopedCore_t::poke(strobe_count, zero_page);
    const std::array<uint8_t, 192> empty_tables{};
    TestFixtures::ScopedCore_t::poke(table_high, empty_tables);
    TestFixtures::enter_at({program_base, 0, 0, 0});
  }

  auto begin() -> void { start_cycle = cpu_get_cumulative_cycles(); }

  static auto strobes() -> size_t { return *mem_get_main_ptr(strobe_count); }

  static auto iteration(size_t k) -> uint32_t {
    const uint32_t low = *mem_get_main_ptr(table_low + k);
    const uint32_t middle = *mem_get_main_ptr(table_middle + k);
    const uint32_t high = *mem_get_main_ptr(table_high + k);
    return low | (middle << 8) | (high << 16);
  }

  // The cycle, since power-on, at which the k-th recorded strobe was read.
  auto read_cycle(size_t k) const -> uint64_t {
    const uint64_t n = iteration(k);
    return start_cycle + idle_iteration_cycles * (n - 1) +
           byte_carry_cycles * (n / 256) + word_carry_cycles * (n / 65536) +
           record_cycles * k + read_offset_cycles;
  }

  auto spacing(size_t k) const -> uint64_t {
    return read_cycle(k + 1) - read_cycle(k);
  }

  // The latch read by the 6502 itself: a direct dispatch after frames have
  // run would hand the bus bridge a stale cycle count. LDA $C000 / STA $10 /
  // NOP at $0200, then back into the loop, whose counters are untouched.
  static auto latch() -> uint8_t {
    constexpr uint16_t probe_base = 0x0200;
    const std::array<uint8_t, 6> probe = {0xAD, 0x00, 0xC0, 0x85, 0x10, 0xEA};
    TestFixtures::ScopedCore_t::poke(probe_base, probe);
    TestFixtures::enter_at({probe_base, 0, 0, 0});
    TestFixtures::step_until_pc(probe_base + 5, program_cycle_cap);
    REQUIRE(cpu_get_registers()->pc == probe_base + 5);
    TestFixtures::enter_at({program_base, 0, 0, 0});
    return *mem_get_main_ptr(0x10);
  }
};

// The signed distance of a measured cycle from the one expected, so a failure
// shows how far off it was.
auto error_of(uint64_t value, uint64_t expected) -> int64_t {
  return static_cast<int64_t>(value) - static_cast<int64_t>(expected);
}

// The core takes the model from a process-wide variable that a harness-built
// machine leaves behind; a core built without one states its own.
struct EnhancedIIe_t {
  struct Model_t {
    Apple2Type_t saved = current_apple2_type;
    Model_t() { current_apple2_type = A2TYPE_APPLE2EENHANCED; }
    ~Model_t() { current_apple2_type = saved; }
    Model_t(const Model_t&) = delete;
    auto operator=(const Model_t&) -> Model_t& = delete;
    Model_t(Model_t&&) = delete;
    auto operator=(Model_t&&) -> Model_t& = delete;
  };
  Model_t model;
  TestConfig_t config{TestConfig_t::enhanced_2e_only()};
  TestFixtures::ScopedCore_t core{config};
};

using Frame_t = std::array<uint8_t, frame_size>;

// The frame this card writes after Z is held with the strobe set: the header,
// no key down, the "no repeat armed" word, the latch and the strobe, and the
// caps-down and repeat-on bytes an older reader takes as its state.
auto frame_after_z_held() -> Frame_t {
  Frame_t frame{};
  const std::array<uint8_t, 48> head = {
      0x01, 0x00, 0x00, 0x00, 0x28, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x5A, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  std::memcpy(frame.data(), head.data(), head.size());
  return frame;
}

// The frame an earlier card wrote after the same Z with caps off, the rocker
// on, the French table, repeat off and one custom key: one key counted, the
// repeat armed on Z, and the configuration bytes that now live in the host.
auto frame_from_earlier_card() -> Frame_t {
  Frame_t frame{};
  const std::array<uint8_t, 36> head = {
      0x01, 0x00, 0x00, 0x00, 0x28, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
      0x5A, 0x00, 0x00, 0x00, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x5A, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00};
  std::memcpy(frame.data(), head.data(), head.size());
  frame.at(40) = 0x58;
  frame.at(168) = 0x59;
  frame.at(296) = 0x18;
  frame.at(424) = 0x01;
  return frame;
}

auto saved_frame() -> Frame_t {
  Frame_t frame{};
  size_t size = frame.size();
  peripheral_save_state_by_name(0, "Keyboard", frame.data(), &size);
  REQUIRE(size == frame.size());
  return frame;
}

auto load_frame(const void* bytes, size_t size) -> void {
  peripheral_load_state_by_name(0, "Keyboard", bytes, size);
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
    "Keyboard: init refuses a host lacking a member it needs and names the "
    "member, and a host without a log is refused silently") {
  const std::vector<std::pair<const char*, void (*)(HostInterface_t*)>>
      members = {
          {"RegisterDirectIO",
           [](HostInterface_t* h) { h->RegisterDirectIO = nullptr; }},
          {"ReadFloatingBus",
           [](HostInterface_t* h) { h->ReadFloatingBus = nullptr; }},
          {"GetCycles", [](HostInterface_t* h) { h->GetCycles = nullptr; }},
          {"ScheduleEvent",
           [](HostInterface_t* h) { h->ScheduleEvent = nullptr; }},
          {"GetClockHz", [](HostInterface_t* h) { h->GetClockHz = nullptr; }},
          {"GetMachine", [](HostInterface_t* h) { h->GetMachine = nullptr; }},
          {"GetFrameCycles",
           [](HostInterface_t* h) { h->GetFrameCycles = nullptr; }},
      };
  for (const auto& member : members) {
    CAPTURE(member.first);
    BenchHost_t bench;
    member.second(bench.host());
    CHECK(bench.create() == nullptr);
    CHECK(bench.last_log().find(member.first) != std::string::npos);
    CHECK(bench.last_log().find("Keyboard in slot 0") != std::string::npos);
  }

  BenchHost_t silent;
  silent.host()->Log = nullptr;
  silent.host()->GetMachine = nullptr;
  CHECK(silent.create() == nullptr);
  CHECK(silent.last_log().empty());
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
  CHECK(keyboard_card()->query(card, unknown_keyboard_id, &byte, nullptr) ==
        peripheral_error);
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
    "Keyboard: on a //e $C010 is read and written and $C011-$C01F written "
    "only, while on a II Plus every one of the sixteen is read and written, "
    "clears the strobe and reads the undriven bus") {
  BenchHost_t iie(peripheral_machine_apple2e);
  REQUIRE(iie.create() != nullptr);
  CHECK(iie.has_read_handler(addr_keyboard_strobe));
  CHECK(iie.has_write_handler(addr_keyboard_strobe));
  for (uint16_t addr = addr_keyboard_strobe + 1;
       addr <= addr_keyboard_strobe_last; ++addr) {
    CAPTURE(addr);
    CHECK_FALSE(iie.has_read_handler(addr));
    CHECK(iie.has_write_handler(addr));
  }
  REQUIRE(iie.press(4, 'a') == peripheral_ok);
  CHECK(iie.read(addr_keyboard_data) == (0x61 | strobe_bit));
  iie.write(addr_keyboard_strobe_last);
  CHECK(iie.read(addr_keyboard_data) == 0x61);
  REQUIRE(iie.press(4, 'a') == peripheral_ok);
  // Any-key-down over the code, the strobe cleared by the read.
  CHECK(iie.read(addr_keyboard_strobe) == (0x61 | strobe_bit));
  CHECK(iie.read(addr_keyboard_data) == 0x61);
  REQUIRE(iie.release(4) == peripheral_ok);
  CHECK(iie.read(addr_keyboard_strobe) == 0x61);

  BenchHost_t ii_plus(peripheral_machine_apple2_plus);
  REQUIRE(ii_plus.create() != nullptr);
  for (uint16_t addr = addr_keyboard_strobe; addr <= addr_keyboard_strobe_last;
       ++addr) {
    CAPTURE(addr);
    CHECK(ii_plus.has_read_handler(addr));
    CHECK(ii_plus.has_write_handler(addr));
  }
  REQUIRE(ii_plus.press(4, 'a') == peripheral_ok);
  // The II keyboard produces upper case only.
  CHECK(ii_plus.read(addr_keyboard_data) == ('A' | strobe_bit));
  CHECK(ii_plus.read(addr_keyboard_strobe + 1) == bus_marker);
  CHECK(ii_plus.read(addr_keyboard_data) == 'A');
  REQUIRE(ii_plus.press(5, '[') == peripheral_ok);
  CHECK(ii_plus.read(addr_keyboard_data) == ('[' | strobe_bit));
  CHECK(ii_plus.read(addr_keyboard_strobe) == bus_marker);
  CHECK(ii_plus.read(addr_keyboard_data) == '[');
}

TEST_CASE(
    "Keyboard: REPT alone strobes once on a II with the latch unchanged and "
    "does nothing on a II Plus or a //e") {
  BenchHost_t ii(peripheral_machine_apple2);
  REQUIRE(ii.create() != nullptr);
  REQUIRE(ii.press(4, 'A') == peripheral_ok);
  REQUIRE(ii.release(4) == peripheral_ok);
  ii.write(addr_keyboard_strobe);
  REQUIRE(ii.read(addr_keyboard_data) == 'A');
  REQUIRE(ii.rept(true) == peripheral_ok);
  CHECK(ii.read(addr_keyboard_data) == ('A' | strobe_bit));
  REQUIRE(ii.rept(false) == peripheral_ok);

  for (PeripheralMachine_t machine :
       {peripheral_machine_apple2_plus, peripheral_machine_apple2e}) {
    CAPTURE(static_cast<int>(machine));
    BenchHost_t bench(machine);
    REQUIRE(bench.create() != nullptr);
    REQUIRE(bench.press(4, 'A') == peripheral_ok);
    REQUIRE(bench.release(4) == peripheral_ok);
    bench.write(addr_keyboard_strobe);
    REQUIRE(bench.read(addr_keyboard_data) == 'A');
    REQUIRE(bench.rept(true) == peripheral_ok);
    CHECK(bench.read(addr_keyboard_data) == 'A');
    REQUIRE(bench.rept(false) == peripheral_ok);
  }
}

TEST_CASE(
    "Keyboard: through the bridge, the 6502 reads the code under the strobe "
    "at $C000 until an access to $C010-$C01F clears the strobe and leaves "
    "the code") {
  EnhancedIIe_t machine;

  press('a', 0x61);

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
    release('a');
    press('a', 0x61);
    // LDA $C000 / STA $13 / <clearer> / LDA $C000 / STA $14 / NOP
    const std::array<uint8_t, 14> program = {
        0xAD,       0x00, 0xC0, 0x85, 0x13, clearer[0], clearer[1],
        clearer[2], 0xAD, 0x00, 0xC0, 0x85, 0x14,       0xEA};
    run_program(program);
    CHECK(*mem_get_main_ptr(0x13) == 0xE1);
    CHECK(*mem_get_main_ptr(0x14) == 0x61);
  }
}

TEST_CASE(
    "Keyboard: $C010 bit 7 says whether any key is held, through a second "
    "key, a stray release, sixteen keys and a seventeenth that evicts the "
    "first, and release_all") {
  EnhancedIIe_t machine;
  REQUIRE_FALSE(any_key_down());

  press(1, 'A');
  CHECK(any_key_down());
  press(2, 'B');
  release(2);
  CHECK(any_key_down());
  // A release for a key never pressed changes nothing.
  release(99);
  CHECK(any_key_down());
  release(1);
  CHECK_FALSE(any_key_down());

  // Seventeen keys: the first is evicted, so letting the other sixteen go
  // leaves nothing held, and the first's own release is then ignored.
  for (uint32_t key = 1; key <= 17; ++key) {
    press(key, 'A');
  }
  CHECK(any_key_down());
  for (uint32_t key = 2; key <= 17; ++key) {
    release(key);
  }
  CHECK_FALSE(any_key_down());
  release(1);
  CHECK_FALSE(any_key_down());

  // Sixteen keys held and the first released: the rest are still down.
  for (uint32_t key = 1; key <= 16; ++key) {
    press(key, 'A');
  }
  release(1);
  CHECK(any_key_down());
  for (uint32_t key = 2; key <= 16; ++key) {
    release(key);
  }
  CHECK_FALSE(any_key_down());

  press(1, 'A');
  press(2, 'B');
  linapple_set_key_release_all();
  settle();
  CHECK_FALSE(any_key_down());
  // The latch keeps the last code; the strobe was cleared by the $C010 reads.
  CHECK(keyboard_data() == 'B');
}

TEST_CASE(
    "Keyboard: a hard reset is power-on and clears the latch, the strobe and "
    "the held keys, while a soft reset leaves the card alone") {
  EnhancedIIe_t machine;

  press(1, 'A');
  REQUIRE(keyboard_data() == ('A' | strobe_bit));
  linapple_reset_soft();
  CHECK(keyboard_data() == ('A' | strobe_bit));
  CHECK(any_key_down());

  linapple_reset_hard();
  CHECK(keyboard_data() == 0);
  CHECK_FALSE(any_key_down());
  // The key held through the reset is gone from the set; its release is
  // ignored and a new press counts.
  release(1);
  CHECK_FALSE(any_key_down());
  press(1, 'A');
  CHECK(any_key_down());
}

TEST_CASE(
    "Keyboard: on a //e a held key repeats from the frame 32 or more frames "
    "after its press on F3's edge and then every four frames, with the latch "
    "unchanged, through a second key, past the first key's release, under "
    "the host's warp, and stops when every key is up") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  harness.boot();

  StrobeObserver_t observer;
  StrobeObserver_t::install();
  observer.begin();
  const uint64_t press_frame = observer.start_cycle / ntsc_frame_cycles;
  const uint64_t first_frame =
      press_frame + 32 + ((16 - press_frame % 16) % 16);

  press(4, 'A');
  harness.run_frames(70);
  // The press's own strobe is the table's first entry.
  REQUIRE(StrobeObserver_t::strobes() >= 7);
  {
    const int64_t error =
        error_of(observer.read_cycle(1), first_frame * ntsc_frame_cycles);
    CAPTURE(error);
    CHECK(error >= -observer_tolerance);
    CHECK(error <= observer_tolerance);
  }
  for (size_t k = 1; k + 1 < StrobeObserver_t::strobes(); ++k) {
    const int64_t error = error_of(observer.spacing(k), repeat_period_cycles);
    CAPTURE(k);
    CAPTURE(error);
    CHECK(error >= -observer_tolerance);
    CHECK(error <= observer_tolerance);
  }

  // A second key restarts the delay from its own frame, and the first key's
  // release while the second is held changes nothing.
  const uint64_t second_frame = cpu_get_cumulative_cycles() / ntsc_frame_cycles;
  const uint64_t second_first_frame =
      second_frame + 32 + ((16 - second_frame % 16) % 16);
  press(5, 'B');
  release(4);
  const size_t before_second = StrobeObserver_t::strobes();
  harness.run_frames(static_cast<uint32_t>(second_first_frame - second_frame) +
                     10);
  REQUIRE(StrobeObserver_t::strobes() >= before_second + 3);
  // Entry before_second is the second key's own strobe; the next is its
  // first repeat.
  {
    const int64_t error = error_of(observer.read_cycle(before_second + 1),
                                   second_first_frame * ntsc_frame_cycles);
    CAPTURE(error);
    CHECK(error >= -observer_tolerance);
    CHECK(error <= observer_tolerance);
    const int64_t spacing_error =
        error_of(observer.spacing(before_second + 1), repeat_period_cycles);
    CAPTURE(spacing_error);
    CHECK(spacing_error >= -observer_tolerance);
    CHECK(spacing_error <= observer_tolerance);
  }
  CHECK((StrobeObserver_t::latch() & 0x7F) == 'B');

  // Every key up: the repeat stops.
  release(5);
  const size_t after_release = StrobeObserver_t::strobes();
  harness.run_frames(40);
  CHECK(StrobeObserver_t::strobes() == after_release);

  // The host's warp runs the machine faster; the repeat keeps its frames.
  press(4, 'A');
  const size_t before_warp = StrobeObserver_t::strobes();
  linapple_set_speed(emulation_speed_max);
  harness.run_frames(1);
  linapple_speed_reset();
  REQUIRE(StrobeObserver_t::strobes() >= before_warp + 4);
  // Entry before_warp is the press's own strobe.
  for (size_t k = before_warp + 1; k + 1 < StrobeObserver_t::strobes(); ++k) {
    const int64_t error = error_of(observer.spacing(k), repeat_period_cycles);
    CAPTURE(k);
    CAPTURE(error);
    CHECK(error >= -observer_tolerance);
    CHECK(error <= observer_tolerance);
  }
  CHECK((StrobeObserver_t::latch() & 0x7F) == 'A');
  release(4);
}

TEST_CASE(
    "Keyboard: a II Plus never repeats a held key by itself; REPT with a key "
    "held strobes one period after and then at the keyboard's 15 Hz, REPT "
    "alone does nothing, and REPT up stops") {
  TestConfig_t::Description_t description = TestConfig_t::enhanced_2e_only();
  description.machine_type = TestConfig_t::machine_apple2_plus;
  TestConfig_t config(description);
  HeadlessHarness_t harness(config);
  harness.boot();

  StrobeObserver_t observer;
  StrobeObserver_t::install();
  observer.begin();

  press(4, 'A');
  harness.run_frames(120);
  CHECK(StrobeObserver_t::strobes() == 1);

  const uint64_t rept_period =
      static_cast<uint64_t>(linapple_get_clock_hz() / 15.0);
  const uint64_t rept_cycle = cpu_get_cumulative_cycles();
  linapple_set_rept(true);
  settle();
  harness.run_frames(60);
  REQUIRE(StrobeObserver_t::strobes() >= 8);
  {
    const int64_t error =
        error_of(observer.read_cycle(1), rept_cycle + rept_period);
    CAPTURE(error);
    CHECK(error >= -observer_tolerance);
    CHECK(error <= observer_tolerance);
  }
  for (size_t k = 1; k + 1 < StrobeObserver_t::strobes(); ++k) {
    const int64_t error = error_of(observer.spacing(k), rept_period);
    CAPTURE(k);
    CAPTURE(error);
    CHECK(error >= -observer_tolerance);
    CHECK(error <= observer_tolerance);
  }
  CHECK((StrobeObserver_t::latch() & 0x7F) == 'A');

  linapple_set_rept(false);
  settle();
  const size_t after_rept_up = StrobeObserver_t::strobes();
  harness.run_frames(30);
  CHECK(StrobeObserver_t::strobes() == after_rept_up);

  release(4);
  linapple_set_rept(true);
  settle();
  harness.run_frames(30);
  CHECK(StrobeObserver_t::strobes() == after_rept_up);
  linapple_set_rept(false);
  settle();
}

TEST_CASE(
    "Keyboard: on a II Plus a read of $C011 clears the strobe and returns "
    "the undriven bus, and $C010 reads the undriven bus, never the latch") {
  TestConfig_t::Description_t description = TestConfig_t::enhanced_2e_only();
  description.machine_type = TestConfig_t::machine_apple2_plus;
  TestConfig_t config(description);
  HeadlessHarness_t harness(config);
  harness.boot();

  // The text page full of normal spaces makes the undriven bus a literal $A0
  // whatever the scanner is fetching.
  const std::array<uint8_t, 0x0400> spaces = [] {
    std::array<uint8_t, 0x0400> page{};
    page.fill(0xA0);
    return page;
  }();
  TestFixtures::ScopedCore_t::poke(0x0400, spaces);
  TestFixtures::ScopedCore_t::poke(0x1400, spaces);

  press(4, 'A');
  // LDA $C000 / STA $10 / LDA $C011 / STA $11 / LDA $C000 / STA $12 /
  // LDA $C010 / STA $13 / NOP
  const std::array<uint8_t, 21> program = {
      0xAD, 0x00, 0xC0, 0x85, 0x10, 0xAD, 0x11, 0xC0, 0x85, 0x11, 0xAD,
      0x00, 0xC0, 0x85, 0x12, 0xAD, 0x10, 0xC0, 0x85, 0x13, 0xEA};
  run_program(program);
  CHECK(*mem_get_main_ptr(0x10) == ('A' | strobe_bit));
  CHECK(*mem_get_main_ptr(0x11) == 0xA0);
  CHECK(*mem_get_main_ptr(0x12) == 'A');
  CHECK(*mem_get_main_ptr(0x13) == 0xA0);
  release(4);
}

TEST_CASE(
    "Keyboard: the frame written with Z held and the strobe set is the "
    "552-byte literal, with no key down and no repeat armed") {
  EnhancedIIe_t machine;

  press('Z', 0x5A);
  CHECK(saved_frame() == frame_after_z_held());

  // The key up and the strobe cleared by the $C010 read: byte 25 is the one
  // that differs, since the held set is never written.
  REQUIRE(any_key_down());
  release('Z');
  CHECK_FALSE(any_key_down());
  Frame_t released = frame_after_z_held();
  released.at(25) = 0;
  CHECK(saved_frame() == released);
}

TEST_CASE(
    "Keyboard: a frame of the wrong size, version or struct_size is refused "
    "with $C000 and $C010 untouched, a longer buffer with a valid frame "
    "loads, and the frame an earlier card wrote restores the latch and strobe "
    "alone") {
  EnhancedIIe_t machine;

  press(4, 'A');
  REQUIRE(keyboard_data() == ('A' | strobe_bit));

  const Frame_t earlier = frame_from_earlier_card();
  load_frame(nullptr, earlier.size());
  CHECK(keyboard_data() == ('A' | strobe_bit));
  load_frame(earlier.data(), earlier.size() - 1);
  CHECK(keyboard_data() == ('A' | strobe_bit));
  Frame_t wrong_version = earlier;
  wrong_version.at(0) = 2;
  load_frame(wrong_version.data(), wrong_version.size());
  CHECK(keyboard_data() == ('A' | strobe_bit));
  Frame_t wrong_size = earlier;
  wrong_size.at(4) = 0xF4;
  wrong_size.at(5) = 0x01;
  load_frame(wrong_size.data(), wrong_size.size());
  CHECK(keyboard_data() == ('A' | strobe_bit));
  CHECK(any_key_down());

  // The earlier card counted one key down and had its repeat armed on Z;
  // neither is the machine's, so after the load no key is down.
  press(4, 'A');
  load_frame(earlier.data(), earlier.size());
  CHECK(keyboard_data() == (0x5A | strobe_bit));
  CHECK_FALSE(any_key_down());
  CHECK(keyboard_data() == 0x5A);

  press(4, 'A');
  std::array<uint8_t, 600> longer{};
  std::memcpy(longer.data(), earlier.data(), earlier.size());
  load_frame(longer.data(), longer.size());
  CHECK(keyboard_data() == (0x5A | strobe_bit));
  CHECK_FALSE(any_key_down());
}

TEST_CASE(
    "Keyboard: the .aws an earlier build wrote loads on a machine with nothing "
    "in any slot and gives back the latch and strobe, the rocker and the "
    "host's custom table untouched") {
  EnhancedIIe_t machine;
  keyboard_apply_custom_mappings();
  REQUIRE(keyboard_custom_switch(4) == keyboard_custom_switch_none);
  REQUIRE_FALSE(linapple_get_rocker_switch());

  press(4, 'A');
  const std::string path =
      TestFixtures::get_fixture_path("keyboard-14249ec3.aws");
  save_state_set_filename(path.c_str());
  REQUIRE(save_state_load());

  CHECK(keyboard_data() == (0x5A | strobe_bit));
  CHECK_FALSE(any_key_down());
  CHECK_FALSE(linapple_get_rocker_switch());
  // The file's custom key 4 typed X; the host's table still knows nothing of
  // it, so A is A.
  const KeyboardHostKey_t host_a = {4, 'a', false, false};
  uint8_t code = 0;
  REQUIRE(keyboard_translate(&host_a, &code));
  CHECK(code == 0x41);
}

TEST_CASE(
    "Keyboard: an .aws written by this build keeps the keyboard region where "
    "every file has it and gives back the latch and strobe with no key down "
    "though one was held at the save") {
  EnhancedIIe_t machine;
  TestFixtures::ScopedTempDir_t dir("linapple_keyboard_test_");
  const std::string path = dir.path() + "/keyboard.aws";

  press('Z', 0x5A);
  save_state_set_filename(path.c_str());
  save_state_save();

  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  in.seekg(80);
  Frame_t region{};
  in.read(reinterpret_cast<char*>(region.data()), region.size());
  REQUIRE(in.good());
  CHECK(region == frame_after_z_held());

  release('Z');
  press(5, 'Q');
  REQUIRE(any_key_down());
  REQUIRE(keyboard_data() == 'Q');

  REQUIRE(save_state_load());
  CHECK(keyboard_data() == (0x5A | strobe_bit));
  CHECK_FALSE(any_key_down());
}
