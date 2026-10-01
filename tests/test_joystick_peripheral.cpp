// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

extern "C" auto joystick_abi_c_descriptor() -> Peripheral_t*;
extern "C" auto joystick_abi_c_state_size() -> size_t;
extern "C" auto joystick_abi_c_trigger_cycle_offset() -> size_t;
extern "C" auto joystick_abi_c_trigger_cycle_size() -> size_t;
extern "C" auto joystick_abi_c_x_pos_offset() -> size_t;
extern "C" auto joystick_abi_c_y_pos_offset() -> size_t;
extern "C" auto joystick_abi_c_buttons_offset() -> size_t;
extern "C" auto joystick_abi_c_trim_x_offset() -> size_t;
extern "C" auto joystick_abi_c_trim_y_offset() -> size_t;
extern "C" auto joystick_abi_c_axis_payload_size() -> size_t;
extern "C" auto joystick_abi_c_button_payload_size() -> size_t;
extern "C" auto joystick_abi_c_state_version() -> uint32_t;

namespace {

constexpr int slot0 = 0;

// The 74LS251 behind the "6" line puts one of eight inputs on D7: inputs 1-3
// are the pushbuttons, 4-7 the four NE558 timers, and A3 does not reach the
// part, so $C068-$C06F are $C060-$C067 again (Apple II Reference Manual, 1979,
// p. 99; Sather, Understanding the Apple II, 7-8; Apple IIe Technical
// Reference Manual, p. 189). Any access to the "7" line, $C070-$C07F, triggers
// the timers (1979 manual pp. 78-79 and 99; IIe Tech Ref pp. 29-30 and 187).
// $C07F stays the motherboard's: on the //e its read is RDDHIRES.
constexpr uint16_t addr_switch0 = 0xC061;
constexpr uint16_t addr_switch1 = 0xC062;
constexpr uint16_t addr_switch2 = 0xC063;
constexpr uint16_t addr_paddle0 = 0xC064;
constexpr uint16_t addr_paddle1 = 0xC065;
constexpr uint16_t addr_mirror_switch0 = 0xC069;
constexpr uint16_t addr_mirror_paddle0 = 0xC06C;
constexpr uint16_t addr_trigger_first = 0xC070;
constexpr uint16_t addr_trigger_last = 0xC07E;
constexpr uint16_t addr_rddhires = 0xC07F;
constexpr uint8_t bit7 = 0x80;

// PREAD, at $FB1E in every monitor ROM under res/roms (bytes AD 70 C0 A0 00
// EA EA BD 64 C0 10 04 C8 D0 F8 88 60, identical in all ten images; Apple II
// Reference Manual, 1979, pp. 63-64; IIe Tech Ref p. 222).
constexpr uint16_t rom_pread = 0xFB1E;

// Programs run from $0300 and single-instruction probes from $0380, both in
// the page the Monitor leaves to the user.
constexpr uint16_t program_base = 0x0300;
constexpr uint16_t probe_base = 0x0380;

// A scanner byte with bit 7 clear shows the bus bits pass; one with bit 7 set
// is what proves the card masks bit 7 before it drives it.
constexpr uint8_t marker_low = 0x5A;
constexpr uint8_t marker_high = 0xDA;

// Switch sources (JoystickButtonPayload_t::source).
constexpr uint8_t source_connector = 0;
constexpr uint8_t source_keyboard = 1;
constexpr uint8_t shift_line = 2;

// A standard two-button controller pulls PB0 and PB1 down through the 560 ohm
// resistors in its plug and leaves PB2 open, which a TTL input reads as 1
// (Sather 7-9 and 7-11; TI, Designing With Logic, SDYA009C, section 3).
constexpr uint8_t default_pulldowns = 0x03;
constexpr uint8_t every_line_pulled_down = 0x07;

// Far past any pulse a fresh counter could have seen, so a first strobe there
// finds every timer expired; 1,000,000 is $0F4240.
constexpr uint64_t far_counter = 1000000;

// Position 255 is the longest pulse the card makes, 11 x 255 + 10 = 2,815
// cycles; three thousand cycles later every channel is idle again.
constexpr uint64_t longest_pulse_and_then_some = 3000;

// PREAD's open-pot exit is 2,829 cycles after its strobe; the cap catches a
// runaway without cutting a legitimate read short.
constexpr uint32_t pread_cycle_cap = 4000;
constexpr uint32_t probe_cycle_cap = 64;
// The Monitor's reset routine is done inside a tenth of a second.
constexpr uint32_t reset_routine_cycle_cap = 100000;

// The Enhanced //e reset routine reads $C062 and, with bit 7 set, jumps to the
// self-test at $C600 (bytes AD 62 C0 10 03 4C 00 C6 at $C2BB of
// res/roms/Apple2e_Enhanced.rom; IIe Tech Ref pp. 90-91).
constexpr uint16_t rom_self_test = 0xC600;

constexpr uint32_t unknown_joystick_id = PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x00FF;

enum class Order_t : uint8_t { keyboard_first, joystick_first };

auto operator<<(std::ostream& out, Order_t order) -> std::ostream& {
  return out << (order == Order_t::keyboard_first ? "keyboard first"
                                                  : "joystick first");
}

const std::initializer_list<Order_t> both_orders = {Order_t::keyboard_first,
                                                    Order_t::joystick_first};

// The card as the registry hands it out, which is what the machine wires in.
auto game_port() -> Peripheral_t* {
  Peripheral_t* descriptor = peripheral_find_internal("linapple.joystick");
  REQUIRE(descriptor != nullptr);
  return descriptor;
}

auto keyboard_card() -> Peripheral_t* {
  Peripheral_t* descriptor = peripheral_find_internal("linapple.keyboard");
  REQUIRE(descriptor != nullptr);
  return descriptor;
}

using Frame_t = std::array<uint8_t, sizeof(JoystickSaveState_t)>;

// Header: version 1, struct_size 56 ($38). A cold start has every timer
// expired, so one strobe at cycle 1,000,000 ($0F4240) triggers all four. The
// positions, switch levels and trim are not the port's and go out as zeros.
// Bytes as a little-endian host lays the frame out.
constexpr Frame_t frame_after_one_strobe = {{
    0x01, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00,  //
    0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
}};

// Positions 0, 10, 100 and 255 are pulses of 10, 120, 1,110 and 2,815 cycles.
// Strobes at 1,000,000, 1,001,200, 1,001,400 and 1,001,450: the second finds
// channels 0-2 expired (1,200 is past 10, 120 and 1,110) and channel 3 not;
// the third finds 0 and 1 expired (200 is past 10 and 120, short of 1,110);
// the fourth only channel 0 (50 is past 10, short of 120). So the triggers are
// 1,001,450 ($0F47EA), 1,001,400 ($0F47B8), 1,001,200 ($0F46F0) and 1,000,000
// ($0F4240).
constexpr Frame_t frame_after_four_strobes = {{
    0x01, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00,  //
    0xEA, 0x47, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0xB8, 0x47, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0xF0, 0x46, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
}};

auto axis_payload(uint8_t paddle, uint8_t position) -> JoystickAxisPayload_t {
  JoystickAxisPayload_t payload{};
  payload.joystick = static_cast<uint8_t>(paddle / 2);
  payload.axis = static_cast<uint8_t>(paddle % 2);
  payload.value = position;
  return payload;
}

auto button_payload(uint8_t line, uint8_t source, bool down)
    -> JoystickButtonPayload_t {
  JoystickButtonPayload_t payload{};
  payload.button = line;
  payload.down = down ? 1 : 0;
  payload.source = source;
  return payload;
}

// The game port in an Enhanced //e built the way a frontend builds it, the
// internal cards in slot 0 through the registry, and the 6502 stepped one
// instruction at a time so that every I/O access is charged at the cumulative
// count the instruction began with. Commands go through the manager's queue
// and are drained by one think, as a running machine drains them once a frame.
struct GamePortMachine_t {
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;
  uint8_t bus_marker = marker_low;

  GamePortMachine_t()
      : config(TestFixtures::ScopedTestConfig_t::enhanced_2e_only()),
        core(config) {
    linapple_reset_hard();
    move_off_cycle_zero();
  }

  // Both slot-0 cards registered by hand through the public API in the order
  // asked for, so a case can show the answer does not depend on it.
  explicit GamePortMachine_t(Order_t order)
      : config(TestFixtures::ScopedTestConfig_t::enhanced_2e_only()),
        core(config) {
    peripheral_manager_init();
    if (order == Order_t::keyboard_first) {
      REQUIRE(peripheral_register(keyboard_card(), slot0) == 0);
      REQUIRE(peripheral_register(game_port(), slot0) == 0);
    } else {
      REQUIRE(peripheral_register(game_port(), slot0) == 0);
      REQUIRE(peripheral_register(keyboard_card(), slot0) == 0);
    }
    linapple_reset_hard();
    move_off_cycle_zero();
  }

  // A strobe charged at cumulative cycle 0 would be recorded as cycle 1, so
  // the first instruction a fresh core runs is one that touches no I/O.
  auto move_off_cycle_zero() -> void {
    constexpr uint8_t nop = 0xEA;
    TestFixtures::ScopedCore_t::poke(probe_base, &nop, 1);
    TestFixtures::enter_at({probe_base, 0, 0, 0});
    static_cast<void>(cpu_execute(0));
    REQUIRE(cpu_get_cumulative_cycles() > 0);
  }

  // The queue is drained only by a think, which the stepping loop never
  // reaches on its own.
  static auto send(uint32_t id, const void* payload, size_t size) -> void {
    REQUIRE(peripheral_command(slot0, id, payload, size) == peripheral_ok);
    peripheral_manager_think(0);
  }

  static auto set_paddle(uint8_t paddle, uint8_t position) -> void {
    const JoystickAxisPayload_t payload = axis_payload(paddle, position);
    send(JOYSTICK_CMD_SET_AXIS, &payload, sizeof(payload));
  }

  static auto set_switch(uint8_t line, uint8_t source, bool down) -> void {
    const JoystickButtonPayload_t payload = button_payload(line, source, down);
    send(JOYSTICK_CMD_SET_BUTTON, &payload, sizeof(payload));
  }

  static auto set_pulldowns(uint8_t mask) -> void {
    send(JOYSTICK_CMD_SET_PULLDOWNS, &mask, sizeof(mask));
  }

  static auto set_shift_key_mod(uint8_t jumper) -> void {
    send(JOYSTICK_CMD_SET_SHIFT_KEY_MOD, &jumper, sizeof(jumper));
  }

  // With one instruction per cpu_execute every access reaches the bus with a
  // per-call count of zero, and while no frame runs the scanner stands still,
  // so one byte at its address is what every read's bits 0-6 return.
  auto place_bus_marker() const -> void {
    TestFixtures::ScopedCore_t::poke(video_get_scanner_address(nullptr, 0),
                                     &bus_marker, 1);
  }

  // One LDA abs from RAM; the bridge hands the card the counter as it stands
  // when the instruction starts, and the instruction's four cycles follow.
  auto read(uint16_t addr) -> uint8_t {
    place_bus_marker();
    const std::array<uint8_t, 3> lda = {0xAD, static_cast<uint8_t>(addr & 0xFF),
                                        static_cast<uint8_t>(addr >> 8)};
    TestFixtures::ScopedCore_t::poke(probe_base, lda);
    TestFixtures::enter_at({probe_base, 0, 0, 0});
    static_cast<void>(cpu_execute(0));
    REQUIRE(cpu_get_registers()->pc ==
            static_cast<uint16_t>(probe_base + lda.size()));
    return cpu_get_registers()->a;
  }

  auto read_at(uint16_t addr, uint64_t counter) -> uint8_t {
    g_cumulative_cycles = counter;
    return read(addr);
  }

  auto high_at(uint16_t addr, uint64_t counter) -> bool {
    return (read_at(addr, counter) & bit7) != 0;
  }

  // One STA abs from RAM.
  auto write_at(uint16_t addr, uint64_t counter) -> void {
    g_cumulative_cycles = counter;
    const std::array<uint8_t, 3> sta = {0x8D, static_cast<uint8_t>(addr & 0xFF),
                                        static_cast<uint8_t>(addr >> 8)};
    TestFixtures::ScopedCore_t::poke(probe_base, sta);
    TestFixtures::enter_at({probe_base, 0, 0, 0});
    static_cast<void>(cpu_execute(0));
    REQUIRE(cpu_get_registers()->pc ==
            static_cast<uint16_t>(probe_base + sta.size()));
  }

  auto strobe_at(uint64_t counter) -> void {
    static_cast<void>(read_at(addr_trigger_first, counter));
  }

  static auto let_every_timer_fall() -> void {
    g_cumulative_cycles += longest_pulse_and_then_some;
  }

  // A program from $0300 with X set, stepped until the PC lands on stop_pc or
  // the cap is spent; returns the cycles spent.
  template <size_t N>
  auto run(const std::array<uint8_t, N>& program, uint16_t stop_pc, uint8_t x,
           uint32_t cap) -> uint32_t {
    place_bus_marker();
    TestFixtures::ScopedCore_t::poke(program_base, program);
    TestFixtures::enter_at({program_base, 0, x, 0});
    return TestFixtures::step_until_pc(stop_pc, cap);
  }

  struct Pread_t {
    uint8_t y;
    uint32_t cycles;
    bool returned;
  };

  // LDX #paddle; JSR $FB1E; NOP at $0300: the LDX and the JSR run first, then
  // the cycles are counted from the fetch at $FB1E to the landing on the NOP,
  // so the figure excludes the JSR's six and is PREAD's own.
  auto pread(uint8_t paddle) -> Pread_t {
    const std::array<uint8_t, 6> caller = {0xA2, paddle, 0x20,
                                           0x1E, 0xFB,   0xEA};
    constexpr uint16_t sentinel = program_base + 5;
    constexpr uint32_t ldx_and_jsr_cycles = 8;
    const uint32_t entry_cycles = run(caller, rom_pread, 0, ldx_and_jsr_cycles);
    REQUIRE(entry_cycles == ldx_and_jsr_cycles);
    REQUIRE(cpu_get_registers()->pc == rom_pread);
    const uint32_t cycles =
        TestFixtures::step_until_pc(sentinel, pread_cycle_cap);
    return {cpu_get_registers()->y, cycles,
            cpu_get_registers()->pc == sentinel};
  }

  static auto frame() -> Frame_t {
    Frame_t frame{};
    size_t size = frame.size();
    peripheral_save_state_by_name(slot0, "Joystick", frame.data(), &size);
    REQUIRE(size == frame.size());
    return frame;
  }

  static auto keyboard_mods() -> KeyboardModifiers_t {
    KeyboardModifiers_t mods{};
    size_t size = sizeof(mods);
    REQUIRE(peripheral_query_by_id(slot0, "linapple.keyboard",
                                   keyboard_query_mods, &mods,
                                   &size) == peripheral_ok);
    return mods;
  }
};

struct BenchHandler_t {
  void* instance;
  PeripheralIOHandler read;
  PeripheralIOHandler write;
  PeripheralStrobeHandler_t on_strobe;
};

// A host built by hand, for what the real one cannot show: a member missing,
// the log line it draws, and the status a call returns.
class BenchHost_t {
 public:
  BenchHost_t() {
    s_active = this;
    host_.Log = bench_log;
    host_.AssertIrq = bench_assert_irq;
    host_.RegisterIO = bench_register_io;
    host_.RegisterCxROM = bench_register_cx_rom;
    host_.RegisterExpansionROM = bench_register_expansion_rom;
    host_.RegisterDirectIO = bench_register_direct_io;
    host_.RegisterDirectIOStrobe = bench_register_direct_io_strobe;
    host_.GetCycles = bench_get_cycles;
    host_.ReadFloatingBus = bench_read_floating_bus;
  }

  ~BenchHost_t() {
    for (void* instance : instances_) {
      game_port()->shutdown(instance);
    }
    s_active = nullptr;
  }

  BenchHost_t(const BenchHost_t&) = delete;
  auto operator=(const BenchHost_t&) -> BenchHost_t& = delete;
  BenchHost_t(BenchHost_t&&) = delete;
  auto operator=(BenchHost_t&&) -> BenchHost_t& = delete;

  auto host() -> HostInterface_t* { return &host_; }

  auto create() -> void* {
    void* instance = game_port()->init(slot0, &host_);
    if (instance != nullptr) {
      instances_.push_back(instance);
    }
    return instance;
  }

  auto log_lines() const -> const std::vector<std::string>& { return lines_; }
  auto set_cycles(uint64_t cycles) -> void { cycles_ = cycles; }
  auto bus_marker(uint8_t marker) -> void { marker_ = marker; }

  // A strobe registration answers any access by firing and leaving the bus
  // alone, as the bridge does.
  auto read(uint16_t addr) -> uint8_t {
    auto it = handlers_.find(addr);
    REQUIRE(it != handlers_.end());
    if (it->second.on_strobe != nullptr) {
      it->second.on_strobe(it->second.instance);
      return marker_;
    }
    REQUIRE(it->second.read != nullptr);
    return it->second.read(it->second.instance, 0, addr, 0, 0, 0);
  }

 private:
  HostInterface_t host_{};
  std::map<uint16_t, BenchHandler_t> handlers_;
  std::vector<void*> instances_;
  std::vector<std::string> lines_;
  uint64_t cycles_ = 0;
  uint8_t marker_ = marker_low;

  static BenchHost_t* s_active;

  // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
  // Justification: Log is variadic in the HostInterface_t ABI.
  static auto bench_log(void* instance, PeripheralLogLevel_t level,
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
    s_active->lines_.emplace_back(line.data());
  }
  // NOLINTEND(cppcoreguidelines-pro-type-vararg)

  static auto bench_assert_irq(int slot, bool assert_irq) -> void {
    (void)slot;
    (void)assert_irq;
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

  static auto bench_register_expansion_rom(int slot, uint8_t* rom) -> void {
    (void)slot;
    (void)rom;
  }

  static auto bench_register_direct_io(void* instance, uint16_t addr,
                                       PeripheralIOHandler read,
                                       PeripheralIOHandler write) -> void {
    if (s_active != nullptr) {
      s_active->handlers_[addr] =
          BenchHandler_t{instance, read, write, nullptr};
    }
  }

  static auto bench_register_direct_io_strobe(void* instance, uint16_t addr,
                                              PeripheralStrobeHandler_t strobe)
      -> void {
    if (s_active != nullptr) {
      s_active->handlers_[addr] =
          BenchHandler_t{instance, nullptr, nullptr, strobe};
    }
  }

  static auto bench_get_cycles() -> uint64_t {
    return s_active != nullptr ? s_active->cycles_ : 0;
  }

  static auto bench_read_floating_bus(uint32_t executed_cycles) -> uint8_t {
    (void)executed_cycles;
    return s_active != nullptr ? s_active->marker_ : marker_low;
  }
};

BenchHost_t* BenchHost_t::s_active = nullptr;

auto save_frame(void* instance) -> Frame_t {
  Frame_t frame{};
  size_t size = frame.size();
  REQUIRE(game_port()->save_state(instance, frame.data(), &size) ==
          peripheral_ok);
  REQUIRE(size == frame.size());
  return frame;
}

auto trigger_in(const Frame_t& frame, size_t paddle) -> uint64_t {
  uint64_t trigger = 0;
  std::memcpy(&trigger, &frame.at(8 + (paddle * 8)), sizeof(trigger));
  return trigger;
}

TEST_CASE(
    "Game port: the registry resolves the card by id and by name, and the "
    "descriptor is the motherboard's port") {
  Peripheral_t* descriptor = game_port();
  CHECK(peripheral_find_internal("Joystick") == descriptor);
  CHECK(peripheral_find_internal("linapple.game-port") == nullptr);

  CHECK(descriptor->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::strcmp(descriptor->id, "linapple.joystick") == 0);
  CHECK(std::strcmp(descriptor->name, "Joystick") == 0);
  CHECK(descriptor->compatible_slots == PERIPHERAL_MASK_INTERNAL);
  CHECK(descriptor->default_slot == 0);

  CHECK(descriptor->init != nullptr);
  CHECK(descriptor->shutdown != nullptr);
  CHECK(descriptor->save_state != nullptr);
  CHECK(descriptor->load_state != nullptr);
  CHECK(descriptor->command != nullptr);
  CHECK(descriptor->query != nullptr);
  // RESET' reaches neither the NE558 (its RESET pin is unused, Sather 7-11)
  // nor a switch contact, and nothing in the port counts time on its own.
  CHECK(descriptor->reset == nullptr);
  CHECK(descriptor->think == nullptr);
  CHECK(descriptor->on_vblank == nullptr);
}

TEST_CASE(
    "Game port: a host missing one of the four members the port needs gets "
    "no card and hears which") {
  BenchHost_t bench;
  CHECK(game_port()->init(slot0, nullptr) == nullptr);

  struct Missing_t {
    const char* name;
    void (*strip)(HostInterface_t*);
  };
  const std::array<Missing_t, 4> members = {{
      {"RegisterDirectIO",
       [](HostInterface_t* h) { h->RegisterDirectIO = nullptr; }},
      {"RegisterDirectIOStrobe",
       [](HostInterface_t* h) { h->RegisterDirectIOStrobe = nullptr; }},
      {"GetCycles", [](HostInterface_t* h) { h->GetCycles = nullptr; }},
      {"ReadFloatingBus",
       [](HostInterface_t* h) { h->ReadFloatingBus = nullptr; }},
  }};
  for (const Missing_t& member : members) {
    CAPTURE(member.name);
    HostInterface_t partial = *bench.host();
    member.strip(&partial);
    const size_t logged_before = bench.log_lines().size();
    CHECK(game_port()->init(slot0, &partial) == nullptr);
    REQUIRE(bench.log_lines().size() == logged_before + 1);
    CHECK(bench.log_lines().back().find(member.name) != std::string::npos);
    CHECK(bench.log_lines().back().find("slot 0") != std::string::npos);
  }

  // A host that cannot log is refused all the same, silently.
  HostInterface_t mute = *bench.host();
  mute.Log = nullptr;
  mute.GetCycles = nullptr;
  const size_t logged_before = bench.log_lines().size();
  CHECK(game_port()->init(slot0, &mute) == nullptr);
  CHECK(bench.log_lines().size() == logged_before);

  CHECK(bench.create() != nullptr);
}

TEST_CASE(
    "Game port: PREAD returns the position for every paddle at 0, 1, 127, "
    "254 and 255, in 11p + 23 cycles from its first fetch") {
  GamePortMachine_t machine;

  // Strobe at the read cycle T0 of LDA $C070; LDY, NOP, NOP and the fourth
  // cycle of LDA $C064,X put sample 0 at T0 + 10, and the loop (LDA 4, BPL
  // not taken 2, INY 2, BNE taken 3) samples every 11 cycles after that
  // (Sather 7-24). Sample k is high while 10 + 11k < 11p + 10, so the first
  // low sample is k = p and Y = p. Exit on sample p: BPL taken 3 and RTS 6
  // complete at T0 + 11p + 19; T0 is the fourth cycle of the fetch at $FB1E,
  // so the run from that fetch to the landing on the return address is
  // 11p + 23 cycles: 23, 34, 1,420, 2,817 and 2,828.
  struct Golden_t {
    uint8_t position;
    uint32_t cycles;
  };
  const std::array<Golden_t, 5> goldens = {{
      {0, 23},
      {1, 34},
      {127, 1420},
      {254, 2817},
      {255, 2828},
  }};
  for (uint8_t paddle = 0; paddle < 4; ++paddle) {
    for (const Golden_t& golden : goldens) {
      CAPTURE(paddle);
      CAPTURE(golden.position);
      GamePortMachine_t::set_paddle(paddle, golden.position);
      GamePortMachine_t::let_every_timer_fall();
      const GamePortMachine_t::Pread_t result = machine.pread(paddle);
      REQUIRE(result.returned);
      CHECK(result.y == golden.position);
      CHECK(result.cycles == golden.cycles);
    }
  }
}

TEST_CASE(
    "Game port: reading paddle 1 right after paddle 0 returns 243, not 255, "
    "because the second strobe finds its timer still high") {
  GamePortMachine_t machine;
  GamePortMachine_t::set_paddle(0, 10);
  GamePortMachine_t::set_paddle(1, 255);
  GamePortMachine_t::let_every_timer_fall();

  // LDX #0; JSR $FB1E; LDX #1; JSR $FB1E; NOP. The first strobe at T0 starts
  // both timers; paddle 0's falls at T0 + 120 and PREAD returns 10 at T0 +
  // 130. LDX (2) and JSR (6) put the second strobe at T0 + 141, where timer 1
  // has 2,815 - 141 = 2,674 cycles to run and is not restarted; its first
  // sample is at 10 and the first low one is k = ceil((2674 - 10) / 11) =
  // 243. Y is read by stopping at the PC after each JSR: a STY to capture it
  // would widen the gap to 144 and give 242.
  const std::array<uint8_t, 11> stream = {0xA2, 0x00, 0x20, 0x1E, 0xFB, 0xA2,
                                          0x01, 0x20, 0x1E, 0xFB, 0xEA};
  constexpr uint16_t after_first_jsr = program_base + 5;
  constexpr uint16_t after_second_jsr = program_base + 10;
  static_cast<void>(machine.run(stream, after_first_jsr, 0, pread_cycle_cap));
  REQUIRE(cpu_get_registers()->pc == after_first_jsr);
  CHECK(cpu_get_registers()->y == 10);
  static_cast<void>(
      TestFixtures::step_until_pc(after_second_jsr, pread_cycle_cap));
  REQUIRE(cpu_get_registers()->pc == after_second_jsr);
  CHECK(cpu_get_registers()->y == 243);
}

TEST_CASE(
    "Game port: a strobe and a sample nine and ten cycles apart read a "
    "ten-cycle pulse high, then low") {
  GamePortMachine_t machine;
  GamePortMachine_t::set_paddle(0, 0);

  // Position 0 is a 10-cycle pulse, so a sample 9 cycles after the strobe is
  // the last high one and a sample at 10 the first low one. LDA $C070 (4),
  // LDA $00 (3), NOP (2) put the sample instruction's start at T0 + 9; LDA
  // $C070 (4), NOP, NOP, NOP (6) put it at T0 + 10. Each sample is taken
  // with LDA $C064,X (X = 0) and again with LDA $C064, so the two addressing
  // modes are charged alike. A later charge of the sample would read the
  // first low, an earlier one the second high.
  const std::array<uint8_t, 10> nine_indexed = {0xAD, 0x70, 0xC0, 0xA5, 0x00,
                                                0xEA, 0xBD, 0x64, 0xC0, 0xEA};
  const std::array<uint8_t, 10> nine_absolute = {0xAD, 0x70, 0xC0, 0xA5, 0x00,
                                                 0xEA, 0xAD, 0x64, 0xC0, 0xEA};
  const std::array<uint8_t, 10> ten_indexed = {0xAD, 0x70, 0xC0, 0xEA, 0xEA,
                                               0xEA, 0xBD, 0x64, 0xC0, 0xEA};
  const std::array<uint8_t, 10> ten_absolute = {0xAD, 0x70, 0xC0, 0xEA, 0xEA,
                                                0xEA, 0xAD, 0x64, 0xC0, 0xEA};
  constexpr uint16_t after_sample = program_base + 9;
  constexpr uint32_t probe_cycles = 13;

  GamePortMachine_t::let_every_timer_fall();
  CHECK(machine.run(nine_indexed, after_sample, 0, probe_cycle_cap) ==
        probe_cycles);
  REQUIRE(cpu_get_registers()->pc == after_sample);
  CHECK((cpu_get_registers()->a & bit7) == bit7);

  GamePortMachine_t::let_every_timer_fall();
  CHECK(machine.run(nine_absolute, after_sample, 0, probe_cycle_cap) ==
        probe_cycles);
  REQUIRE(cpu_get_registers()->pc == after_sample);
  CHECK((cpu_get_registers()->a & bit7) == bit7);

  GamePortMachine_t::let_every_timer_fall();
  CHECK(machine.run(ten_indexed, after_sample, 0, probe_cycle_cap) ==
        probe_cycles + 1);
  REQUIRE(cpu_get_registers()->pc == after_sample);
  CHECK((cpu_get_registers()->a & bit7) == 0);

  GamePortMachine_t::let_every_timer_fall();
  CHECK(machine.run(ten_absolute, after_sample, 0, probe_cycle_cap) ==
        probe_cycles + 1);
  REQUIRE(cpu_get_registers()->pc == after_sample);
  CHECK((cpu_get_registers()->a & bit7) == 0);
}

TEST_CASE(
    "Game port: every address from $C070 to $C07E strobes an idle timer, "
    "read or written, and $C07F does not") {
  GamePortMachine_t machine;
  GamePortMachine_t::set_paddle(0, 0);

  // Position 0 is a 10-cycle pulse: high nine cycles after the strobe, low
  // at ten; by the next address the timer is idle again.
  constexpr uint64_t last_high = 9;
  constexpr uint64_t first_low = 10;
  uint64_t counter = far_counter;
  for (uint16_t addr = addr_trigger_first; addr <= addr_trigger_last; ++addr) {
    CAPTURE(addr);
    static_cast<void>(machine.read_at(addr, counter));
    CHECK(machine.high_at(addr_paddle0, counter + last_high));
    CHECK_FALSE(machine.high_at(addr_paddle0, counter + first_low));
    counter += longest_pulse_and_then_some;

    machine.write_at(addr, counter);
    CHECK(machine.high_at(addr_paddle0, counter + last_high));
    CHECK_FALSE(machine.high_at(addr_paddle0, counter + first_low));
    counter += longest_pulse_and_then_some;
  }

  static_cast<void>(machine.read_at(addr_rddhires, counter));
  CHECK_FALSE(machine.high_at(addr_paddle0, counter + 4));
}

TEST_CASE(
    "Game port: a strobe during a pulse leaves that channel's fall time "
    "where it was") {
  GamePortMachine_t machine;

  // Paddle 0 at 100 is an 11 x 100 + 10 = 1,110-cycle pulse; paddle 1 at 0 a
  // 10-cycle one. A second strobe 500 cycles in finds paddle 1 idle and
  // restarts it, and finds paddle 0 high and leaves it: it falls at 1,110,
  // not at 500 + 1,110 (Sather 7-24; NE558 datasheet, output independent of
  // trigger conditions).
  GamePortMachine_t::set_paddle(0, 100);
  GamePortMachine_t::set_paddle(1, 0);
  machine.strobe_at(far_counter);
  machine.strobe_at(far_counter + 500);
  CHECK(machine.high_at(addr_paddle1, far_counter + 509));
  CHECK_FALSE(machine.high_at(addr_paddle1, far_counter + 510));
  CHECK(machine.high_at(addr_paddle0, far_counter + 1109));
  CHECK_FALSE(machine.high_at(addr_paddle0, far_counter + 1110));
}

TEST_CASE("Game port: $C069 to $C06F read as $C061 to $C067") {
  GamePortMachine_t machine;

  // With every line pulled down a press is visible on all three.
  GamePortMachine_t::set_pulldowns(every_line_pulled_down);
  for (uint8_t line = 0; line < 3; ++line) {
    CAPTURE(line);
    const uint16_t base = static_cast<uint16_t>(addr_switch0 + line);
    const uint16_t mirror = static_cast<uint16_t>(addr_mirror_switch0 + line);
    CHECK_FALSE(machine.high_at(mirror, far_counter));
    CHECK_FALSE(machine.high_at(base, far_counter));
    GamePortMachine_t::set_switch(line, source_connector, true);
    CHECK(machine.high_at(mirror, far_counter));
    CHECK(machine.high_at(base, far_counter));
    GamePortMachine_t::set_switch(line, source_connector, false);
  }

  // Every paddle at the centre, 127, is an 11 x 127 + 10 = 1,407-cycle pulse.
  constexpr uint64_t centre_pulse = 1407;
  machine.strobe_at(far_counter);
  for (uint8_t paddle = 0; paddle < 4; ++paddle) {
    CAPTURE(paddle);
    const uint16_t base = static_cast<uint16_t>(addr_paddle0 + paddle);
    const uint16_t mirror = static_cast<uint16_t>(addr_mirror_paddle0 + paddle);
    CHECK(machine.high_at(mirror, far_counter + centre_pulse - 1));
    CHECK(machine.high_at(base, far_counter + centre_pulse - 1));
    CHECK_FALSE(machine.high_at(mirror, far_counter + centre_pulse));
    CHECK_FALSE(machine.high_at(base, far_counter + centre_pulse));
  }
}

TEST_CASE("Game port: a position changed mid-pulse moves that channel's fall") {
  GamePortMachine_t machine;

  // The capacitor charges through the pot it has now, so a pulse started at
  // 10 (120 cycles) and moved to 200 runs to 11 x 200 + 10 = 2,210, and one
  // started at 200 and moved to 10 is over at 120.
  GamePortMachine_t::set_paddle(0, 10);
  machine.strobe_at(far_counter);
  GamePortMachine_t::set_paddle(0, 200);
  CHECK(machine.high_at(addr_paddle0, far_counter + 500));
  CHECK(machine.high_at(addr_paddle0, far_counter + 2209));
  CHECK_FALSE(machine.high_at(addr_paddle0, far_counter + 2210));

  const uint64_t second = far_counter + longest_pulse_and_then_some;
  machine.strobe_at(second);
  GamePortMachine_t::set_paddle(0, 10);
  CHECK(machine.high_at(addr_paddle0, second + 119));
  CHECK_FALSE(machine.high_at(addr_paddle0, second + 120));
}

TEST_CASE(
    "Game port: bits 0-6 are the floating bus and bit 7 the timer or the "
    "switch, on a paddle and on a button") {
  GamePortMachine_t machine;

  // Paddle 0 at the centre is a 1,407-cycle pulse. With scanner byte $5A a
  // charging timer reads $DA and an expired one $5A; with scanner byte $DA
  // the same two reads give $DA then $5A, which is the mask at work (IIe
  // Tech Ref p. 41: bits 0-6 are whatever the bus holds).
  constexpr uint64_t centre_pulse = 1407;
  machine.bus_marker = marker_low;
  machine.strobe_at(far_counter);
  CHECK(machine.read_at(addr_paddle0, far_counter + 1) == marker_high);
  CHECK(machine.read_at(addr_paddle0, far_counter + centre_pulse) ==
        marker_low);

  machine.bus_marker = marker_high;
  const uint64_t second = far_counter + longest_pulse_and_then_some;
  machine.strobe_at(second);
  CHECK(machine.read_at(addr_paddle0, second + 1) == marker_high);
  CHECK(machine.read_at(addr_paddle0, second + centre_pulse) == marker_low);

  // The same on PB0, pulled down: pressed $DA, released $5A, with either
  // scanner byte.
  machine.bus_marker = marker_low;
  GamePortMachine_t::set_switch(0, source_connector, true);
  CHECK(machine.read(addr_switch0) == marker_high);
  GamePortMachine_t::set_switch(0, source_connector, false);
  CHECK(machine.read(addr_switch0) == marker_low);
  machine.bus_marker = marker_high;
  GamePortMachine_t::set_switch(0, source_connector, true);
  CHECK(machine.read(addr_switch0) == marker_high);
  GamePortMachine_t::set_switch(0, source_connector, false);
  CHECK(machine.read(addr_switch0) == marker_low);

  // PB2 is open with the default mask and reads 1 at rest.
  machine.bus_marker = marker_low;
  CHECK(machine.read(addr_switch2) == marker_high);
}

TEST_CASE(
    "Game port: each switch line reads the OR of its button and its key over "
    "the pull-down, whichever slot-0 card registered first") {
  for (Order_t order : both_orders) {
    CAPTURE(order);
    GamePortMachine_t machine(order);

    // PB0 and PB1 rest low through the controller's pull-downs; the //e
    // wires Open Apple and Solid Apple in parallel with them (IIe Tech Ref
    // pp. 13 and 41), so either switch, or both, drives the line high. With
    // the pull-down gone the open input reads 1 with nothing down.
    for (uint8_t line = 0; line < 2; ++line) {
      CAPTURE(line);
      const uint16_t addr = static_cast<uint16_t>(addr_switch0 + line);
      CHECK_FALSE(machine.high_at(addr, far_counter));
      GamePortMachine_t::set_switch(line, source_connector, true);
      CHECK(machine.high_at(addr, far_counter));
      GamePortMachine_t::set_switch(line, source_keyboard, true);
      CHECK(machine.high_at(addr, far_counter));
      GamePortMachine_t::set_switch(line, source_connector, false);
      CHECK(machine.high_at(addr, far_counter));
      GamePortMachine_t::set_switch(line, source_keyboard, false);
      CHECK_FALSE(machine.high_at(addr, far_counter));

      const uint8_t without_this_line =
          static_cast<uint8_t>(default_pulldowns & ~(1U << line));
      GamePortMachine_t::set_pulldowns(without_this_line);
      CHECK(machine.high_at(addr, far_counter));
      GamePortMachine_t::set_switch(line, source_connector, true);
      CHECK(machine.high_at(addr, far_counter));
      GamePortMachine_t::set_switch(line, source_connector, false);
      GamePortMachine_t::set_pulldowns(default_pulldowns);
      CHECK_FALSE(machine.high_at(addr, far_counter));
    }

    // PB2, mod off: open with the default mask, so 1 at rest and 1 with the
    // button; a pull-down on it makes the button visible; and the shift key
    // is not wired to it, so shift down leaves the line as it was.
    CHECK(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_connector, true);
    CHECK(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_connector, false);
    GamePortMachine_t::set_switch(shift_line, source_keyboard, true);
    CHECK(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_keyboard, false);

    GamePortMachine_t::set_pulldowns(every_line_pulled_down);
    CHECK_FALSE(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_connector, true);
    CHECK(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_connector, false);
    GamePortMachine_t::set_switch(shift_line, source_keyboard, true);
    CHECK_FALSE(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_keyboard, false);
    GamePortMachine_t::set_pulldowns(default_pulldowns);

    // PB2, mod on: the single-wire shift-key mod grounds the line through the
    // shift key (IIe Tech Ref p. 41; Sather 7-31), so shift down reads 0 and
    // shift up 1 whatever the connector button and the mask do.
    GamePortMachine_t::set_shift_key_mod(1);
    CHECK(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_keyboard, true);
    CHECK_FALSE(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_connector, true);
    CHECK_FALSE(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_keyboard, false);
    CHECK(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_connector, false);
    GamePortMachine_t::set_pulldowns(every_line_pulled_down);
    CHECK(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_keyboard, true);
    CHECK_FALSE(machine.high_at(addr_switch2, far_counter));
    GamePortMachine_t::set_switch(shift_line, source_keyboard, false);
    GamePortMachine_t::set_pulldowns(default_pulldowns);
    GamePortMachine_t::set_shift_key_mod(0);

    // The keyboard card's own modifier levels answer the debugger's query and
    // reach no switch line; the Solid Apple switch on PB1 is the game port's.
    KeyboardModifiers_t mods{};
    mods.alt = 1;
    GamePortMachine_t::send(keyboard_cmd_set_mods, &mods, sizeof(mods));
    GamePortMachine_t::set_switch(1, source_keyboard, true);
    CHECK_FALSE(machine.high_at(addr_switch0, far_counter));
    CHECK(machine.high_at(addr_switch1, far_counter));
    CHECK(GamePortMachine_t::keyboard_mods().alt == 1);
    CHECK(GamePortMachine_t::keyboard_mods().gui == 0);
    GamePortMachine_t::set_switch(1, source_keyboard, false);
    CHECK_FALSE(machine.high_at(addr_switch1, far_counter));
  }
}

TEST_CASE(
    "Game port: Open Apple held through a hard reset reads high at the first "
    "instruction after it") {
  GamePortMachine_t machine;
  GamePortMachine_t::set_switch(0, source_keyboard, true);
  linapple_reset_hard();
  CHECK((machine.read(addr_switch0) & bit7) == bit7);
  CHECK((machine.read(addr_switch1) & bit7) == 0);

  // The switch is a contact: a reset with the key up reads it up.
  GamePortMachine_t::set_switch(0, source_keyboard, false);
  linapple_reset_hard();
  CHECK((machine.read(addr_switch0) & bit7) == 0);
}

TEST_CASE(
    "Game port: the Enhanced //e ROM run from its reset vector with Solid "
    "Apple held reaches the self-test at $C600") {
  GamePortMachine_t machine;

  // Without the key the reset routine passes the check and never lands on
  // $C600 inside the cap; with it, the JMP at $C2C0 is taken.
  linapple_reset_hard();
  REQUIRE(cpu_get_registers()->pc != rom_self_test);
  static_cast<void>(
      TestFixtures::step_until_pc(rom_self_test, reset_routine_cycle_cap));
  CHECK(cpu_get_registers()->pc != rom_self_test);

  GamePortMachine_t::set_switch(1, source_keyboard, true);
  linapple_reset_hard();
  const uint32_t cycles =
      TestFixtures::step_until_pc(rom_self_test, reset_routine_cycle_cap);
  CHECK(cpu_get_registers()->pc == rom_self_test);
  CHECK(cycles < reset_routine_cycle_cap);
}

TEST_CASE("Game port: a running pulse survives a hard reset") {
  GamePortMachine_t machine;

  // Position 255 is a 2,815-cycle pulse. The NE558's RESET pin is unused in
  // the Apple (Sather 7-11), so RESET' lets the pulse run out on its own.
  GamePortMachine_t::set_paddle(0, 255);
  machine.strobe_at(far_counter);
  linapple_reset_hard();
  CHECK(machine.high_at(addr_paddle0, far_counter + 2814));
  CHECK_FALSE(machine.high_at(addr_paddle0, far_counter + 2815));
}

TEST_CASE(
    "Game port: a cold start reads every paddle expired, and so does a "
    "strobe the counter is wound back past") {
  GamePortMachine_t machine;

  // Before the first trigger the NE558's outputs are low (datasheet note 3).
  for (uint8_t paddle = 0; paddle < 4; ++paddle) {
    CAPTURE(paddle);
    CHECK(machine.read(static_cast<uint16_t>(addr_paddle0 + paddle)) ==
          marker_low);
  }

  // A strobe at 1,000,000 with the counter then set to 1,000, as a loaded
  // snapshot does: on a real machine three milliseconds after anything every
  // timer has fallen, so a trigger ahead of the counter reads expired. The
  // frame still carries the 1,000,000 triggers until the next strobe, which
  // finds every channel idle and moves them to 1,000 ($03E8).
  machine.strobe_at(far_counter);
  CHECK(GamePortMachine_t::frame() == frame_after_one_strobe);
  constexpr uint64_t wound_back = 1000;
  for (uint8_t paddle = 0; paddle < 4; ++paddle) {
    CAPTURE(paddle);
    CHECK_FALSE(machine.high_at(static_cast<uint16_t>(addr_paddle0 + paddle),
                                wound_back));
  }
  CHECK(GamePortMachine_t::frame() == frame_after_one_strobe);
  machine.strobe_at(wound_back);
  const Frame_t moved = GamePortMachine_t::frame();
  for (size_t paddle = 0; paddle < 4; ++paddle) {
    CAPTURE(paddle);
    CHECK(trigger_in(moved, paddle) == wound_back);
  }
}

TEST_CASE(
    "Game port: the frame after one strobe from a cold start is the literal") {
  GamePortMachine_t first;
  GamePortMachine_t::set_switch(0, source_connector, true);
  GamePortMachine_t::set_paddle(0, 255);
  first.strobe_at(far_counter);
  CHECK(GamePortMachine_t::frame() == frame_after_one_strobe);
}

TEST_CASE(
    "Game port: four spaced strobes at distinct positions leave four "
    "distinct triggers in the frame") {
  GamePortMachine_t machine;
  GamePortMachine_t::set_paddle(0, 0);
  GamePortMachine_t::set_paddle(1, 10);
  GamePortMachine_t::set_paddle(2, 100);
  GamePortMachine_t::set_paddle(3, 255);
  machine.strobe_at(far_counter);
  machine.strobe_at(far_counter + 1200);
  machine.strobe_at(far_counter + 1400);
  machine.strobe_at(far_counter + 1450);
  CHECK(GamePortMachine_t::frame() == frame_after_four_strobes);
}

TEST_CASE(
    "Game port: a rejected load reports an error and changes nothing, and an "
    "accepted one loads any trigger") {
  BenchHost_t bench;
  void* port = bench.create();
  REQUIRE(port != nullptr);
  bench.set_cycles(far_counter);
  static_cast<void>(bench.read(addr_trigger_first));
  REQUIRE(save_frame(port) == frame_after_one_strobe);

  // Each rejected frame carries different triggers so that a load that
  // slipped through would show in the re-saved frame.
  Frame_t short_frame = frame_after_four_strobes;
  CHECK(game_port()->load_state(port, short_frame.data(),
                                short_frame.size() - 1) == peripheral_error);
  std::array<uint8_t, sizeof(JoystickSaveState_t) + 1> long_frame{};
  std::copy(frame_after_four_strobes.begin(), frame_after_four_strobes.end(),
            long_frame.begin());
  CHECK(game_port()->load_state(port, long_frame.data(), long_frame.size()) ==
        peripheral_error);
  Frame_t bad_version = frame_after_four_strobes;
  bad_version.at(0) = 0x02;
  CHECK(game_port()->load_state(port, bad_version.data(), bad_version.size()) ==
        peripheral_error);
  Frame_t bad_struct_size = frame_after_four_strobes;
  bad_struct_size.at(4) = 0x30;
  CHECK(game_port()->load_state(port, bad_struct_size.data(),
                                bad_struct_size.size()) == peripheral_error);
  CHECK(game_port()->load_state(port, nullptr, short_frame.size()) ==
        peripheral_error);
  CHECK(game_port()->load_state(nullptr, short_frame.data(),
                                short_frame.size()) == peripheral_error);
  CHECK(save_frame(port) == frame_after_one_strobe);

  CHECK(game_port()->load_state(port, frame_after_four_strobes.data(),
                                frame_after_four_strobes.size()) ==
        peripheral_ok);
  CHECK(save_frame(port) == frame_after_four_strobes);

  // Any trigger loads; one the counter can never reach reads expired, as a
  // real machine's timer would three milliseconds after anything.
  Frame_t all_ones = frame_after_one_strobe;
  std::fill(all_ones.begin() + 8, all_ones.begin() + 40, 0xFF);
  CHECK(game_port()->load_state(port, all_ones.data(), all_ones.size()) ==
        peripheral_ok);
  CHECK(save_frame(port) == all_ones);
  for (uint8_t paddle = 0; paddle < 4; ++paddle) {
    CAPTURE(paddle);
    CHECK((bench.read(static_cast<uint16_t>(addr_paddle0 + paddle)) & bit7) ==
          0);
  }

  // The frame's size is asked for with no buffer; a short buffer is refused
  // and so is a missing instance.
  size_t size = 0;
  CHECK(game_port()->save_state(port, nullptr, &size) == peripheral_ok);
  CHECK(size == sizeof(JoystickSaveState_t));
  Frame_t scratch{};
  size = scratch.size() - 1;
  CHECK(game_port()->save_state(port, scratch.data(), &size) ==
        peripheral_error);
  size = scratch.size();
  CHECK(game_port()->save_state(nullptr, scratch.data(), &size) ==
        peripheral_error);
  CHECK(game_port()->save_state(port, scratch.data(), nullptr) ==
        peripheral_error);
}

TEST_CASE(
    "Game port: the jumper and the pull-down mask survive a hard reset and a "
    "loaded frame") {
  GamePortMachine_t machine;

  // Soldered, not state: with every line pulled down and the jumper in, PB2
  // reads 0 at rest and follows the shift key, and neither a reset nor a
  // frame carries either setting away.
  GamePortMachine_t::set_pulldowns(every_line_pulled_down);
  GamePortMachine_t::set_shift_key_mod(1);
  GamePortMachine_t::set_switch(shift_line, source_keyboard, true);
  CHECK_FALSE(machine.high_at(addr_switch2, far_counter));
  GamePortMachine_t::set_switch(shift_line, source_keyboard, false);
  CHECK(machine.high_at(addr_switch2, far_counter));

  linapple_reset_hard();
  GamePortMachine_t::set_switch(shift_line, source_keyboard, true);
  CHECK_FALSE(machine.high_at(addr_switch2, far_counter));
  GamePortMachine_t::set_switch(shift_line, source_keyboard, false);
  CHECK(machine.high_at(addr_switch2, far_counter));

  peripheral_load_state_by_name(slot0, "Joystick",
                                frame_after_four_strobes.data(),
                                frame_after_four_strobes.size());
  CHECK(GamePortMachine_t::frame() == frame_after_four_strobes);
  GamePortMachine_t::set_switch(shift_line, source_keyboard, true);
  CHECK_FALSE(machine.high_at(addr_switch2, far_counter));
  GamePortMachine_t::set_switch(shift_line, source_keyboard, false);
  CHECK(machine.high_at(addr_switch2, far_counter));

  // With the jumper out again the mask is what shows: PB2 pulled down rests
  // at 0 and reads the button.
  GamePortMachine_t::set_shift_key_mod(0);
  CHECK_FALSE(machine.high_at(addr_switch2, far_counter));
  GamePortMachine_t::set_switch(shift_line, source_connector, true);
  CHECK(machine.high_at(addr_switch2, far_counter));
  GamePortMachine_t::set_switch(shift_line, source_connector, false);
  CHECK_FALSE(machine.high_at(addr_switch2, far_counter));
}

TEST_CASE(
    "Game port: an unknown id in its own subsystem and a foreign id are "
    "incompatible, and the wrong payload size is an error") {
  BenchHost_t bench;
  void* port = bench.create();
  REQUIRE(port != nullptr);

  // Indices 2-4 once named a trim, a reset and a host configuration; a sender
  // built against that header is told the card does not know them.
  const std::initializer_list<uint32_t> unknown = {
      unknown_joystick_id,
      PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0002,
      PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0003,
      PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0004,
  };
  std::array<uint8_t, 8> bytes{};
  for (uint32_t id : unknown) {
    CAPTURE(id);
    CHECK(game_port()->command(port, id, bytes.data(), bytes.size()) ==
          peripheral_incompatible);
    CHECK(game_port()->command(port, id, nullptr, 0) ==
          peripheral_incompatible);
  }
  CHECK(game_port()->command(port, keyboard_cmd_set_rocker, bytes.data(), 1) ==
        peripheral_incompatible);
  CHECK(game_port()->command(port, PERIPHERAL_SUBSYSTEM_DISK | 0x0003,
                             bytes.data(),
                             bytes.size()) == peripheral_incompatible);

  const JoystickAxisPayload_t axis = axis_payload(0, 200);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_AXIS, &axis,
                             sizeof(axis)) == peripheral_ok);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_AXIS, &axis,
                             sizeof(axis) - 1) == peripheral_error);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_AXIS, bytes.data(),
                             sizeof(axis) + 1) == peripheral_error);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_AXIS, nullptr,
                             sizeof(axis)) == peripheral_error);

  const JoystickButtonPayload_t button =
      button_payload(0, source_connector, true);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_BUTTON, &button,
                             sizeof(button)) == peripheral_ok);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_BUTTON, &button,
                             sizeof(button) - 1) == peripheral_error);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_BUTTON, bytes.data(),
                             sizeof(button) + 1) == peripheral_error);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_BUTTON, nullptr,
                             sizeof(button)) == peripheral_error);

  const uint8_t one = 1;
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_SHIFT_KEY_MOD, &one, 1) ==
        peripheral_ok);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_SHIFT_KEY_MOD, bytes.data(),
                             2) == peripheral_error);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_SHIFT_KEY_MOD, nullptr,
                             1) == peripheral_error);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_PULLDOWNS, &one, 1) ==
        peripheral_ok);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_PULLDOWNS, &one, 0) ==
        peripheral_error);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_PULLDOWNS, nullptr, 1) ==
        peripheral_error);

  CHECK(game_port()->command(nullptr, JOYSTICK_CMD_SET_AXIS, &axis,
                             sizeof(axis)) == peripheral_error);
}

TEST_CASE(
    "Game port: a button, level, source, joystick, axis, mask or jumper out "
    "of range is refused") {
  BenchHost_t bench;
  void* port = bench.create();
  REQUIRE(port != nullptr);

  // Two joysticks of two axes, three lines, two levels, two sources, a
  // three-bit mask and a one-bit jumper: the last value in range is taken
  // and the first beyond it refused.
  JoystickAxisPayload_t axis = axis_payload(3, 0);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_AXIS, &axis,
                             sizeof(axis)) == peripheral_ok);
  axis.joystick = 2;
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_AXIS, &axis,
                             sizeof(axis)) == peripheral_error);
  axis.joystick = 0;
  axis.axis = 2;
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_AXIS, &axis,
                             sizeof(axis)) == peripheral_error);

  JoystickButtonPayload_t button = button_payload(2, source_keyboard, true);
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_BUTTON, &button,
                             sizeof(button)) == peripheral_ok);
  button.button = 3;
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_BUTTON, &button,
                             sizeof(button)) == peripheral_error);
  button.button = 0;
  button.down = 2;
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_BUTTON, &button,
                             sizeof(button)) == peripheral_error);
  button.down = 1;
  button.source = 2;
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_BUTTON, &button,
                             sizeof(button)) == peripheral_error);

  uint8_t value = every_line_pulled_down;
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_PULLDOWNS, &value, 1) ==
        peripheral_ok);
  value = every_line_pulled_down + 1;
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_PULLDOWNS, &value, 1) ==
        peripheral_error);
  value = 1;
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_SHIFT_KEY_MOD, &value, 1) ==
        peripheral_ok);
  value = 2;
  CHECK(game_port()->command(port, JOYSTICK_CMD_SET_SHIFT_KEY_MOD, &value, 1) ==
        peripheral_error);
}

TEST_CASE("Game port: every query is incompatible") {
  BenchHost_t bench;
  void* port = bench.create();
  REQUIRE(port != nullptr);

  // The port has nothing to answer: its state is read through the switches
  // and the timers. The two retired query indices are among the ids asked.
  const std::initializer_list<uint32_t> queries = {
      PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0000,
      PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0001,
      PERIPHERAL_SUBSYSTEM_JOYSTICK | 0x0002,
      unknown_joystick_id,
      static_cast<uint32_t>(keyboard_query_mods),
      static_cast<uint32_t>(PERIPHERAL_QUERY_AUDIO_INFO),
  };
  for (uint32_t id : queries) {
    CAPTURE(id);
    std::array<uint8_t, 8> answer{};
    size_t size = answer.size();
    CHECK(game_port()->query(port, id, answer.data(), &size) ==
          peripheral_incompatible);
    CHECK(game_port()->query(nullptr, id, answer.data(), &size) ==
          peripheral_incompatible);
  }
  CHECK(game_port()->query(port, unknown_joystick_id, nullptr, nullptr) ==
        peripheral_error);
}

TEST_CASE("Game port: the C99 view of the frame matches the C++ one") {
  CHECK(joystick_abi_c_descriptor() == game_port());
  static_assert(sizeof(JoystickSaveState_t) == 56,
                "the version-1 frame is 56 bytes");
  CHECK(joystick_abi_c_state_size() == 56);
  CHECK(joystick_abi_c_state_size() == sizeof(JoystickSaveState_t));
  CHECK(joystick_abi_c_state_version() == JOYSTICK_STATE_VERSION);

  CHECK(joystick_abi_c_trigger_cycle_offset() == 8);
  CHECK(joystick_abi_c_trigger_cycle_offset() ==
        offsetof(JoystickSaveState_t, trigger_cycle));
  CHECK(joystick_abi_c_trigger_cycle_size() == 32);
  CHECK(joystick_abi_c_trigger_cycle_size() ==
        sizeof(JoystickSaveState_t::trigger_cycle));
  CHECK(joystick_abi_c_x_pos_offset() == 40);
  CHECK(joystick_abi_c_x_pos_offset() == offsetof(JoystickSaveState_t, x_pos));
  CHECK(joystick_abi_c_y_pos_offset() == 42);
  CHECK(joystick_abi_c_y_pos_offset() == offsetof(JoystickSaveState_t, y_pos));
  CHECK(joystick_abi_c_buttons_offset() == 44);
  CHECK(joystick_abi_c_buttons_offset() ==
        offsetof(JoystickSaveState_t, buttons));
  CHECK(joystick_abi_c_trim_x_offset() == 48);
  CHECK(joystick_abi_c_trim_x_offset() ==
        offsetof(JoystickSaveState_t, trim_x));
  CHECK(joystick_abi_c_trim_y_offset() == 50);
  CHECK(joystick_abi_c_trim_y_offset() ==
        offsetof(JoystickSaveState_t, trim_y));

  CHECK(joystick_abi_c_axis_payload_size() == 4);
  CHECK(joystick_abi_c_axis_payload_size() == sizeof(JoystickAxisPayload_t));
  CHECK(joystick_abi_c_button_payload_size() == 4);
  CHECK(joystick_abi_c_button_payload_size() ==
        sizeof(JoystickButtonPayload_t));
}

}  // namespace
