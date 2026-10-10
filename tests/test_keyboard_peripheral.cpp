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
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Crc32.h"
#include "doctest.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/SaveStateManager.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"
#include "test_keyboard_abi_c.h"

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
// The bus byte while the text page is full of normal spaces: bit 7 set.
constexpr uint8_t bus_marker = 0xA0;
constexpr uint16_t program_base = 0x0300;
constexpr uint32_t program_cycle_cap = 1000;
constexpr uint32_t ntsc_frame_cycles = 17030;
// 312 lines of 65 cycles (Sather, Understanding the Apple IIe, 3-7).
constexpr uint32_t pal_frame_cycles = 20280;
constexpr uint64_t repeat_period_frames = 4;
constexpr uint64_t repeat_period_cycles =
    repeat_period_frames * ntsc_frame_cycles;
// A strobe is seen within one observer iteration of its wake, and the wake
// within one instruction of the armed cycle.
constexpr int64_t observer_tolerance = 45;
// The reset routine, Apple-key checks included, is done inside a tenth of a
// second; the bell after it, WAIT $40 and 192 half cycles of WAIT $0C, is
// another 117,000 cycles (Monitor listing, $FBDD).
constexpr uint32_t reset_routine_cycle_cap = 100000;
constexpr uint32_t reset_and_bell_cycle_cap = 250000;
constexpr int text_rows = 24;

// Read from res/roms with xxd against the Monitor listings (Apple II
// Reference Manual 1979; Apple IIe Technical Reference Manual).
constexpr uint16_t rom_reset = 0xFA62;
constexpr uint16_t rom_reset_clears_strobe = 0xFA7E;  // BIT $C010
constexpr uint16_t rom_reset_bell = 0xFF3A;           // JSR $FF3A at $FA82
constexpr uint16_t rom_power_up_byte_good = 0xFA8F;   // past BNE $FAA6
constexpr uint16_t rom_cold_start = 0xFAA6;           // JSR $FB60
constexpr uint16_t rom_keyin = 0xFD1B;
constexpr uint16_t rom_keyin_spin_last = 0xFD25;  // II Plus: INC $4E .. BPL
constexpr uint16_t rom_keyin_read = 0xFD28;       // II Plus: LDA $C000
constexpr uint16_t rom_keyin_clear = 0xFD2B;      // II Plus: BIT $C010
constexpr uint16_t rom_internal_entry = 0xFBB4;   // //e: into the $C100 ROM
constexpr uint16_t rom_2e_keyin_poll = 0xC28B;    // Enhanced: LDA $C000
constexpr uint16_t rom_2e_keyin_clear = 0xC29B;   // Enhanced: STA $C010
// Unenhanced: $C100 sends Y = 6 to $C288, whose cursor-flashing wait at $C2C6
// polls $C000 at $C2DB; the final read is at $C2BE and STA $C010 at $C2C1.
constexpr uint16_t rom_2e_un_keyin_poll = 0xC2DB;
constexpr uint16_t rom_2e_un_keyin_clear = 0xC2C1;
// The Enhanced //e reset's Apple-key reads: LDA $C062 / BPL $C2C3 / JMP $C600
// / LDA $C061 / BPL $C2E2, then two $A0 bytes a page down to page 2 before the
// power-up byte check (IIe Technical Reference pp. 93-95).
constexpr uint16_t rom_2e_read_solid_apple = 0xC2BB;
constexpr uint16_t rom_2e_self_test_jump = 0xC2C0;
constexpr uint16_t rom_2e_read_open_apple = 0xC2C3;
constexpr uint16_t rom_2e_scribble_pages = 0xC2C8;
constexpr uint16_t rom_2e_past_apple_keys = 0xC2E2;
constexpr uint16_t rom_2e_self_test = 0xC600;

// Signed, so a failure shows how far off the cycle was.
auto error_of(uint64_t value, uint64_t expected) -> int64_t {
  return static_cast<int64_t>(value) - static_cast<int64_t>(expected);
}

auto describe(TestConfig_t::MachineType_t model)
    -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description = TestConfig_t::enhanced_2e_only();
  description.machine_type = model;
  return description;
}

// The branches a reset routine took are its order of visits, not a cycle
// literal.
struct Trace_t {
  std::vector<uint16_t> pcs;
  bool reached = false;
  uint32_t cycles = 0;
};

auto trace_until(uint16_t sentinel, uint32_t cap) -> Trace_t {
  Trace_t trace;
  const CpuRegisters_t* regs = cpu_get_registers();
  while (regs->pc != sentinel && trace.cycles < cap) {
    trace.pcs.push_back(regs->pc);
    trace.cycles += cpu_execute(0);
  }
  trace.reached = regs->pc == sentinel;
  return trace;
}

auto visited(const Trace_t& trace, uint16_t pc) -> bool {
  for (uint16_t seen : trace.pcs) {
    if (seen == pc) {
      return true;
    }
  }
  return false;
}

// LDA addr / STA $10 / NOP at $0200, registers put back: a direct dispatch
// after frames or a stepped I/O instruction would hand the bus bridge a stale
// cycle count.
auto peek(uint16_t addr) -> uint8_t {
  constexpr uint16_t probe_base = 0x0200;
  constexpr uint16_t probe_store = 0x0010;
  const CpuRegisters_t saved = *cpu_get_registers();
  const std::array<uint8_t, 6> probe = {0xAD,
                                        static_cast<uint8_t>(addr & 0xFF),
                                        static_cast<uint8_t>(addr >> 8),
                                        0x85,
                                        probe_store,
                                        0xEA};
  TestFixtures::ScopedCore_t::poke(probe_base, probe);
  TestFixtures::enter_at({probe_base, 0, 0, 0});
  TestFixtures::step_until_pc(probe_base + 5, program_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == probe_base + 5);
  *cpu_get_registers() = saved;
  return *mem_get_main_ptr(probe_store);
}

auto find_row(const HeadlessHarness_t& harness, const std::string& text)
    -> int {
  for (int row = 0; row < text_rows; ++row) {
    if (harness.get_text_row(row) == text) {
      return row;
    }
  }
  return -1;
}

auto screen(const HeadlessHarness_t& harness) -> std::vector<std::string> {
  std::vector<std::string> rows;
  for (int row = 0; row < text_rows; ++row) {
    rows.push_back(harness.get_text_row(row));
  }
  return rows;
}

// With no disk controller the Autostart scan falls through to Applesoft.
auto boot_to_prompt(HeadlessHarness_t& harness) -> void {
  constexpr uint32_t prompt_frame_cap = 300;
  harness.boot();
  uint32_t frames = 0;
  while (find_row(harness, "]") < 0 && frames < prompt_frame_cap) {
    harness.run_frames(1);
    ++frames;
  }
  CAPTURE(frames);
  REQUIRE(find_row(harness, "]") >= 0);
}

// Held two frames and released two; the repeat delay is far longer.
auto tap(HeadlessHarness_t& harness, uint8_t code) -> void {
  linapple_set_key_state(code, true);
  harness.run_frames(2);
  linapple_set_key_state(code, false);
  harness.run_frames(2);
}

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

// For what the real host cannot show: the handlers registered, the status
// returned and the line logged.
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

// A think drains the command queue, as a running machine does once a frame.
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

// Stepped to its final NOP, as the Monitor would run it; the byte after the
// last store is the sentinel.
template <size_t N>
auto run_program(const std::array<uint8_t, N>& program) -> void {
  TestFixtures::ScopedCore_t::poke(program_base, program);
  TestFixtures::enter_at({program_base, 0, 0, 0});
  const auto sentinel = static_cast<uint16_t>(program_base + N - 1);
  TestFixtures::step_until_pc(sentinel, program_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == sentinel);
}

// Spins on $C000 bit 7, clears each strobe through $C010 and records the
// 24-bit iteration count at $0380 (low), $03C0 (middle) and $0340 (high); the
// count runs in $08-$0A and the strobes seen in $06. The idle iteration is 15
// cycles, so a strobe's cycle is known to one iteration, which tells 68,120
// from 68,000; a carry into the middle byte costs 7 more, into the high byte
// 11, and a recorded strobe 38, which the arithmetic puts back.
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

  auto read_cycle(size_t k) const -> uint64_t {
    const uint64_t n = iteration(k);
    return start_cycle + idle_iteration_cycles * (n - 1) +
           byte_carry_cycles * (n / 256) + word_carry_cycles * (n / 65536) +
           record_cycles * k + read_offset_cycles;
  }

  auto spacing(size_t k) const -> uint64_t {
    return read_cycle(k + 1) - read_cycle(k);
  }

  // The latch read by the 6502 itself, the loop's counters untouched.
  static auto latch() -> uint8_t { return peek(addr_keyboard_data); }
};

// Runs to a frame boundary whose frame number has the asked phase in F3's
// sixteen-frame period.
auto run_to_phase(HeadlessHarness_t& harness, uint64_t phase,
                  uint64_t frame_cycles) -> uint64_t {
  constexpr uint64_t phase_frames = 16;
  for (uint64_t frames = 0; frames < 2 * phase_frames; ++frames) {
    const uint64_t frame = cpu_get_cumulative_cycles() / frame_cycles;
    if (frame % phase_frames == phase % phase_frames) {
      return frame;
    }
    harness.run_frames(1);
  }
  FAIL("no frame boundary with the asked phase");
  return 0;
}

// The press's own strobe is the first entry; the first repeat lands on F3's
// edge 32 or more frames after the press frame, then every four.
auto check_repeat_from_press(HeadlessHarness_t& harness,
                             StrobeObserver_t& observer, uint64_t press_frame,
                             uint64_t expected_delay_frames,
                             uint64_t frame_cycles) -> void {
  StrobeObserver_t::install();
  observer.begin();
  REQUIRE(observer.start_cycle / frame_cycles == press_frame);
  press(4, 'A');
  const uint64_t first_frame = press_frame + expected_delay_frames;
  const auto frames_to_run =
      static_cast<uint32_t>((expected_delay_frames + 3 * repeat_period_frames) *
                                frame_cycles / ntsc_frame_cycles +
                            2);
  harness.run_frames(frames_to_run);
  REQUIRE(StrobeObserver_t::strobes() >= 4);
  {
    const int64_t error =
        error_of(observer.read_cycle(1), first_frame * frame_cycles);
    CAPTURE(error);
    CHECK(error >= -observer_tolerance);
    CHECK(error <= observer_tolerance);
  }
  for (size_t k = 1; k + 1 < StrobeObserver_t::strobes(); ++k) {
    const int64_t error =
        error_of(observer.spacing(k), repeat_period_frames * frame_cycles);
    CAPTURE(k);
    CAPTURE(error);
    CHECK(error >= -observer_tolerance);
    CHECK(error <= observer_tolerance);
  }
  CHECK((StrobeObserver_t::latch() & 0x7F) == 'A');
  release(4);
}

// The model is process-wide and a harness-built machine leaves it behind.
struct EnhancedIIe_t {
  struct Model_t {
    Apple2Type saved = current_apple2_type;
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

// Header, no key down, the no-repeat word, the latch and strobe, and the
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

// An earlier card's frame after the same Z: caps off, rocker on, the French
// table, repeat off and one custom key, with one key counted and the repeat
// armed on Z.
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

  // The bridge puts the keyboard in slot 0 beside the speaker.
  EnhancedIIe_t machine;
  CHECK(peripheral_present(0, "linapple.keyboard"));
  CHECK(peripheral_present(0, "linapple.speaker"));
  CHECK_FALSE(peripheral_present(1, "linapple.keyboard"));
  CHECK_FALSE(peripheral_present(0, "linapple.no-such-card"));
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
  // STA $12 / NOP: KEYIN's pattern, the strobe cleared only by the BIT (IIe
  // Technical Reference p. 13; Sather IIe 7-4).
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
    "after its press on F3's edge, 32, 43, 33, 32 and 33 frames for presses "
    "at phases 0, 5, 15, 16 and 31 of the flash counter, then every four "
    "frames with the latch unchanged") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  harness.boot();
  StrobeObserver_t observer;

  // Sather's 32 to 48 scans (Understanding the Apple IIe, 2-17) with the
  // delay generator clocked by F3 (3-18): the first repeat falls on F3's edge
  // 32 or more frames after the press frame.
  struct Phase_t {
    uint64_t press_frame;
    uint64_t delay_frames;
  };
  const std::array<Phase_t, 5> phases = {
      {{0, 32}, {5, 43}, {15, 33}, {16, 32}, {31, 33}}};
  for (const Phase_t& phase : phases) {
    CAPTURE(phase.press_frame);
    const uint64_t press_frame =
        run_to_phase(harness, phase.press_frame, ntsc_frame_cycles);
    check_repeat_from_press(harness, observer, press_frame, phase.delay_frames,
                            ntsc_frame_cycles);
  }
}

TEST_CASE(
    "Keyboard: on a PAL //e the repeat counts its frames in 20,280 cycles, so "
    "the strobes come 81,120 cycles apart") {
  TestConfig_t::Description_t description = TestConfig_t::enhanced_2e_only();
  description.extras.push_back({"Configuration", "Video Emulation", "2"});
  TestConfig_t config(description);
  HeadlessHarness_t harness(config);
  harness.boot();
  REQUIRE(system_state.clks_per_frame == pal_frame_cycles);
  StrobeObserver_t observer;

  const uint64_t press_frame = run_to_phase(harness, 0, pal_frame_cycles);
  check_repeat_from_press(harness, observer, press_frame, 32, pal_frame_cycles);
}

TEST_CASE(
    "Keyboard: on a //e the repeat follows the keys held: a second key "
    "restarts the delay from its own frame, the first key's release changes "
    "nothing while the second is held, the host's warp leaves the spacing at "
    "four frames, and it stops when every key is up") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  harness.boot();

  StrobeObserver_t observer;
  StrobeObserver_t::install();
  observer.begin();
  press(4, 'A');
  harness.run_frames(50);
  REQUIRE(StrobeObserver_t::strobes() >= 2);

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
    "Keyboard: a II never repeats a held key by itself; REPT with a key held "
    "strobes one period after and then at the 555's ten a second, REPT alone "
    "strobes once with the latch unchanged, and REPT up stops") {
  TestConfig_t config(describe(TestConfig_t::machine_apple2));
  HeadlessHarness_t harness(config);
  harness.boot();

  StrobeObserver_t observer;
  StrobeObserver_t::install();
  observer.begin();

  press(4, 'A');
  harness.run_frames(120);
  CHECK(StrobeObserver_t::strobes() == 1);

  // The 555 at U3 with R3 = 220 k runs at about ten presses a second, wall
  // time (Apple II Reference Manual 1979, pp. 7 and 102).
  const uint64_t rept_period =
      static_cast<uint64_t>(linapple_get_clock_hz() / 10.0);
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

  // REPT pressed alone duplicates the last code generated (1979 p. 7): one
  // strobe, the latch as it was, and no more.
  release(4);
  linapple_set_rept(true);
  settle();
  harness.run_frames(30);
  CHECK(StrobeObserver_t::strobes() == after_rept_up + 1);
  CHECK((StrobeObserver_t::latch() & 0x7F) == 'A');
  linapple_set_rept(false);
  settle();
  harness.run_frames(10);
  CHECK(StrobeObserver_t::strobes() == after_rept_up + 1);
}

TEST_CASE(
    "Keyboard: on a II Plus a read of $C011 clears the strobe and returns "
    "the undriven bus, and $C010 reads the undriven bus, never the latch") {
  TestConfig_t::Description_t description = TestConfig_t::enhanced_2e_only();
  description.machine_type = TestConfig_t::machine_apple2_plus;
  TestConfig_t config(description);
  HeadlessHarness_t harness(config);
  harness.boot();

  // The text page full of normal spaces makes the undriven bus a literal $A0.
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
    "Keyboard: on a //e a read of $C011 leaves the strobe set and returns the "
    "language card's bank in bit 7, 1 with bank 2 selected and 0 with bank 1") {
  EnhancedIIe_t machine;
  press(4, 'A');

  // LDA $C083 / LDA $C011 / STA $10 / LDA $C000 / STA $11 / LDA $C08B /
  // LDA $C011 / STA $12 / LDA $C000 / STA $13 / NOP: $C080-$C087 select bank
  // 2 and $C088-$C08F bank 1, which $C011 reports (IIe Technical Reference
  // p. 141; Sather IIe 5-24); no read of $C011-$C01F clears the strobe (7-4).
  const std::array<uint8_t, 27> program = {
      0xAD, 0x83, 0xC0, 0xAD, 0x11, 0xC0, 0x85, 0x10, 0xAD,
      0x00, 0xC0, 0x85, 0x11, 0xAD, 0x8B, 0xC0, 0xAD, 0x11,
      0xC0, 0x85, 0x12, 0xAD, 0x00, 0xC0, 0x85, 0x13, 0xEA};
  run_program(program);
  CHECK((*mem_get_main_ptr(0x10) & strobe_bit) != 0);
  CHECK(*mem_get_main_ptr(0x11) == ('A' | strobe_bit));
  CHECK((*mem_get_main_ptr(0x12) & strobe_bit) == 0);
  CHECK(*mem_get_main_ptr(0x13) == ('A' | strobe_bit));
}

TEST_CASE(
    "Keyboard: the II Plus Monitor's KEYIN, entered through JSR $FD1B, returns "
    "a pending key with bit 7 set, clears the strobe and advances the random "
    "seed, and with no key pending spins in its poll loop") {
  TestConfig_t config(describe(TestConfig_t::machine_apple2_plus));
  HeadlessHarness_t harness(config);
  harness.boot();

  // JSR $FD1B / NOP; BASL points the cursor store at the top of the text
  // page, as after a cold start.
  const std::array<uint8_t, 4> caller = {0x20, 0x1B, 0xFD, 0xEA};
  const uint16_t sentinel = program_base + 3;
  const std::array<uint8_t, 2> basl = {0x00, 0x04};
  const std::array<uint8_t, 2> seed = {0x00, 0x00};
  TestFixtures::ScopedCore_t::poke(program_base, caller);
  TestFixtures::ScopedCore_t::poke(0x0028, basl);

  press(4, 'A');
  TestFixtures::ScopedCore_t::poke(0x004E, seed);
  TestFixtures::enter_at({program_base, 0, 0, 0});
  Trace_t pending = trace_until(sentinel, program_cycle_cap);
  REQUIRE(pending.reached);
  CHECK(visited(pending, rom_keyin));
  CHECK(visited(pending, rom_keyin_read));
  CHECK(visited(pending, rom_keyin_clear));
  CHECK(cpu_get_registers()->a == ('A' | strobe_bit));
  CHECK(peek(addr_keyboard_data) == 'A');
  // One pass of INC $4E before the pending key was seen.
  CHECK(*mem_get_main_ptr(0x4E) == 1);
  CHECK(*mem_get_main_ptr(0x4F) == 0);
  release(4);

  // Nothing pending: KEYIN counts in $4E/$4F and polls $C000 until the cap.
  constexpr uint32_t spin_cap = 2000;
  TestFixtures::ScopedCore_t::poke(0x004E, seed);
  TestFixtures::enter_at({program_base, 0, 0, 0});
  Trace_t spinning = trace_until(sentinel, spin_cap);
  CHECK_FALSE(spinning.reached);
  bool entered = false;
  for (uint16_t pc : spinning.pcs) {
    if (pc == rom_keyin) {
      entered = true;
    }
    if (entered) {
      CAPTURE(pc);
      CHECK(pc >= rom_keyin);
      CHECK(pc <= rom_keyin_spin_last);
    }
  }
  CHECK(entered);
  const uint32_t seed_after =
      *mem_get_main_ptr(0x4E) |
      (static_cast<uint32_t>(*mem_get_main_ptr(0x4F)) << 8);
  // Each pass, INC $4E / BNE / BIT $C000 / BPL, is 15 cycles and counts once.
  CHECK(seed_after >= spin_cap / 15 - 2);
}

TEST_CASE(
    "Keyboard: the //e Monitor's KEYIN goes through $FBB4 into the internal "
    "ROM, polls $C000 there, and clears the strobe with STA $C010, on the "
    "Enhanced and the unenhanced ROM") {
  struct Rom_t {
    TestConfig_t::MachineType_t model;
    uint16_t poll;
    uint16_t clear;
  };
  const std::array<Rom_t, 2> roms = {{
      {TestConfig_t::machine_apple2e_enhanced, rom_2e_keyin_poll,
       rom_2e_keyin_clear},
      {TestConfig_t::machine_apple2e, rom_2e_un_keyin_poll,
       rom_2e_un_keyin_clear},
  }};
  for (const Rom_t& rom : roms) {
    CAPTURE(rom.model);
    TestConfig_t config(describe(rom.model));
    HeadlessHarness_t harness(config);
    harness.boot();

    const std::array<uint8_t, 4> caller = {0x20, 0x1B, 0xFD, 0xEA};
    const uint16_t sentinel = program_base + 3;
    const std::array<uint8_t, 2> basl = {0x00, 0x04};
    TestFixtures::ScopedCore_t::poke(program_base, caller);
    TestFixtures::ScopedCore_t::poke(0x0028, basl);

    press(4, 'A');
    TestFixtures::enter_at({program_base, 0, 0, 0});
    constexpr uint32_t keyin_cap = 2000;
    Trace_t trace = trace_until(sentinel, keyin_cap);
    REQUIRE(trace.reached);
    CHECK(visited(trace, rom_internal_entry));
    CHECK(visited(trace, rom.poll));
    CHECK(visited(trace, rom.clear));
    CHECK(cpu_get_registers()->a == ('A' | strobe_bit));
    CHECK(peek(addr_keyboard_data) == 'A');
    release(4);
  }
}

TEST_CASE(
    "Keyboard: Applesoft's GETLN on a machine with no disk controller echoes "
    "what is typed and runs it, a $21 arrives as the exclamation mark, and a "
    "$03 at the prompt breaks nothing") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  boot_to_prompt(harness);

  harness.type_string("PRINT 2+2\r", 2);
  harness.run_frames(10);
  const int echo = find_row(harness, "]PRINT 2+2");
  REQUIRE(echo >= 0);
  CHECK(harness.get_text_row(echo + 1) == "4");

  harness.type_string("PRINT \"!\"\r", 2);
  harness.run_frames(10);
  const int bang = find_row(harness, "]PRINT \"!\"");
  REQUIRE(bang >= 0);
  CHECK(harness.get_text_row(bang + 1) == "!");

  // Applesoft looks for Ctrl-C between statements of a running program, not
  // at its prompt, where GETLN takes it as one more character of the line.
  const std::vector<std::string> before = screen(harness);
  tap(harness, 0x03);
  harness.run_frames(10);
  CHECK(screen(harness) == before);
  for (const std::string& row : screen(harness)) {
    CHECK(row.find("BREAK") == std::string::npos);
  }
  // Ctrl-X throws the line away and the prompt is as it was.
  tap(harness, 0x18);
  harness.run_frames(10);
  harness.type_string("PRINT 3*3\r", 2);
  harness.run_frames(10);
  const int again = find_row(harness, "]PRINT 3*3");
  REQUIRE(again >= 0);
  CHECK(harness.get_text_row(again + 1) == "9");
}

TEST_CASE(
    "Keyboard: a running Applesoft program breaks on code $03 and on no other, "
    "and its output pauses on $13 at a carriage return until any key") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  boot_to_prompt(harness);

  harness.type_string("10 GOTO 10\r", 2);
  harness.type_string("RUN\r", 2);
  harness.run_frames(10);
  tap(harness, 'A');
  harness.run_frames(10);
  CHECK(find_row(harness, "BREAK IN 10") < 0);
  // ISCNTC: LDA $C000 / CMP #$83 (Applesoft at $D858).
  tap(harness, 0x03);
  harness.run_frames(10);
  CHECK(find_row(harness, "BREAK IN 10") >= 0);
  REQUIRE(find_row(harness, "]") >= 0);

  harness.type_string("FOR I=1 TO 500:PRINT I:NEXT\r", 2);
  harness.run_frames(3);
  // After a carriage return COUT1 looks at $C000 for $93, clears the strobe
  // at $FB85 and waits at $FB88 for a fresh one ($FB78-$FB94); the key is let
  // go inside the repeat delay, so nothing re-strobes.
  linapple_set_key_state(0x13, true);
  harness.run_frames(3);
  linapple_set_key_state(0x13, false);
  harness.run_frames(3);
  const std::vector<std::string> paused = screen(harness);
  CHECK(find_row(harness, "]") < 0);
  harness.run_frames(30);
  CHECK(screen(harness) == paused);

  tap(harness, 'A');
  harness.run_frames(30);
  CHECK(screen(harness) != paused);
  // 500 lines scroll in about 770 frames.
  constexpr uint32_t finish_frame_cap = 1200;
  uint32_t frames = 0;
  while (find_row(harness, "]") < 0 && frames < finish_frame_cap) {
    harness.run_frames(10);
    frames += 10;
  }
  CAPTURE(frames);
  CHECK(find_row(harness, "]") >= 0);
  CHECK(find_row(harness, "500") >= 0);
}

TEST_CASE(
    "Keyboard: through the Monitor, a soft reset leaves the latch and strobe "
    "to BIT $C010 at $FA7E, and a hard reset reaches $FA7E, the bell and, with "
    "no Apple key held, $C2E2, never the self-test or the page scribble, with "
    "no key down afterwards") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  harness.boot();

  press(4, 'A');
  REQUIRE(peek(addr_keyboard_data) == ('A' | strobe_bit));
  harness.reset_soft();
  REQUIRE(cpu_get_registers()->pc == rom_reset);
  Trace_t soft = trace_until(rom_reset_clears_strobe, reset_routine_cycle_cap);
  REQUIRE(soft.reached);
  // RESET' leaves the latch alone (Sather, Understanding the Apple II, 6-17;
  // Understanding the Apple IIe, 7-5): the strobe is still set here.
  CHECK(peek(addr_keyboard_data) == ('A' | strobe_bit));
  CHECK((peek(addr_keyboard_strobe) & strobe_bit) != 0);
  cpu_execute(0);
  CHECK(peek(addr_keyboard_data) == 'A');
  release(4);

  press(4, 'A');
  REQUIRE((peek(addr_keyboard_strobe) & strobe_bit) != 0);
  linapple_reset_hard();
  Trace_t hard = trace_until(rom_reset_bell, reset_routine_cycle_cap);
  REQUIRE(hard.reached);
  CHECK(visited(hard, rom_2e_read_solid_apple));
  CHECK(visited(hard, rom_2e_read_open_apple));
  CHECK(visited(hard, rom_2e_past_apple_keys));
  CHECK(visited(hard, rom_reset_clears_strobe));
  CHECK_FALSE(visited(hard, rom_2e_self_test_jump));
  CHECK_FALSE(visited(hard, rom_2e_scribble_pages));
  CHECK_FALSE(visited(hard, rom_2e_self_test));
  // Power-on: the key held before it is gone from the set.
  CHECK(peek(addr_keyboard_data) == 0);
  CHECK((peek(addr_keyboard_strobe) & strobe_bit) == 0);
  release(4);
}

TEST_CASE(
    "Keyboard: the Apple keys at reset on a //e with its keyboard: Open Apple "
    "takes the page scribble to the cold start, Solid Apple the self-test, "
    "neither the warm start, and letting every key go clears Open Apple too") {
  TestConfig_t config(TestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  // Whether a game port card shares the lines changes nothing below.
  const bool game_port = peripheral_present(0, "linapple.joystick");
  CAPTURE(game_port);
  // A booted machine holds a valid power-up byte, so a reset with no Apple key
  // takes the warm start through ($3F2) rather than the cold start.
  boot_to_prompt(harness);

  harness.reset_soft();
  Trace_t plain = trace_until(rom_power_up_byte_good, reset_and_bell_cycle_cap);
  REQUIRE(plain.reached);
  CHECK(visited(plain, rom_2e_past_apple_keys));
  CHECK_FALSE(visited(plain, rom_2e_self_test_jump));
  CHECK_FALSE(visited(plain, rom_2e_scribble_pages));
  CHECK_FALSE(visited(plain, rom_cold_start));

  // Open Apple: two $A0 bytes a page from $BF down to $02 take the power-up
  // byte with them, so the check at $FA85 fails and the cold start follows
  // (IIe Technical Reference pp. 93-94).
  linapple_set_modifiers(false, false, true, false);
  settle();
  harness.reset_soft();
  Trace_t open_apple = trace_until(rom_cold_start, reset_and_bell_cycle_cap);
  REQUIRE(open_apple.reached);
  CHECK(visited(open_apple, rom_2e_scribble_pages));
  CHECK_FALSE(visited(open_apple, rom_2e_self_test_jump));
  CHECK_FALSE(visited(open_apple, rom_2e_self_test));
  CHECK_FALSE(visited(open_apple, rom_power_up_byte_good));

  // Both up again: $C2E2 and no scribble, no self-test.
  linapple_set_modifiers(false, false, false, false);
  settle();
  harness.reset_soft();
  Trace_t released =
      trace_until(rom_2e_past_apple_keys, reset_routine_cycle_cap);
  REQUIRE(released.reached);
  CHECK_FALSE(visited(released, rom_2e_self_test_jump));
  CHECK_FALSE(visited(released, rom_2e_scribble_pages));

  // Open Apple held, then focus lost: the release of every key lets go of the
  // Apple keys too, and the next reset reads them up.
  linapple_set_modifiers(false, false, true, false);
  settle();
  linapple_set_key_release_all();
  settle();
  harness.reset_soft();
  Trace_t let_go = trace_until(rom_2e_past_apple_keys, reset_routine_cycle_cap);
  REQUIRE(let_go.reached);
  CHECK_FALSE(visited(let_go, rom_2e_scribble_pages));
  CHECK_FALSE(visited(let_go, rom_2e_self_test_jump));

  // Solid Apple: the self-test titles the top row and fills the screen with
  // its RAM patterns, which no boot does.
  linapple_set_modifiers(false, false, false, true);
  settle();
  harness.reset_soft();
  Trace_t solid_apple = trace_until(rom_2e_self_test, reset_routine_cycle_cap);
  REQUIRE(solid_apple.reached);
  CHECK(visited(solid_apple, rom_2e_self_test_jump));
  CHECK_FALSE(visited(solid_apple, rom_2e_read_open_apple));
  bool titled = false;
  for (int frame = 0; frame < 3 && !titled; ++frame) {
    harness.run_frames(1);
    titled = harness.get_text_row(0).find("Apple //e") != std::string::npos;
  }
  CHECK(titled);
  harness.run_frames(10);
  CHECK(harness.get_text_row(0).find("Apple //e") == std::string::npos);
  linapple_set_modifiers(false, false, false, false);
  settle();
}

namespace {

// The national character ROM is chosen when the core comes up, so the
// language is set before the machine.
struct GermanCharacterRom_t {
  Apple2Language saved = linapple_get_language();
  GermanCharacterRom_t() { linapple_set_language(A2LANG_DE); }
  ~GermanCharacterRom_t() { linapple_set_language(saved); }
  GermanCharacterRom_t(const GermanCharacterRom_t&) = delete;
  auto operator=(const GermanCharacterRom_t&) -> GermanCharacterRom_t& = delete;
  GermanCharacterRom_t(GermanCharacterRom_t&&) = delete;
  auto operator=(GermanCharacterRom_t&&) -> GermanCharacterRom_t& = delete;
};

auto frame_crc32() -> uint32_t {
  constexpr size_t frame_pixels = 560 * 384;
  video_redraw_screen();
  const uint32_t* pixels = video_get_output_buffer();
  REQUIRE(pixels != nullptr);
  return crc32_compute(pixels, frame_pixels * sizeof(uint32_t));
}

}  // namespace

TEST_CASE(
    "Keyboard: the rocker switch is machine state the bridge keeps, and the "
    "video follows it: with the German character ROM a row of letters and "
    "the national code points renders as one half of the ROM with the switch "
    "off and the other with it on") {
  GermanCharacterRom_t german;
  EnhancedIIe_t machine;
  REQUIRE_FALSE(linapple_get_rocker_switch());

  // Row 0, normal video: the letters and the eight code points the German
  // keyboard ROM's local half gives its own glyphs ($40, $5B-$5D, $7B-$7E),
  // with the top bit set.
  const char* text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ @[\\]{|}~ 1234";
  std::array<uint8_t, 40> row{};
  for (size_t i = 0; i < row.size(); ++i) {
    row.at(i) = static_cast<uint8_t>(text[i] | 0x80);
  }
  TestFixtures::ScopedCore_t::poke(0x0400, row);

  const uint32_t rocker_off = frame_crc32();
  linapple_set_rocker_switch(true);
  CHECK(linapple_get_rocker_switch());
  const uint32_t rocker_on = frame_crc32();
  CHECK(rocker_off != rocker_on);
  CHECK(rocker_off == 0x1B2C00AB);
  CHECK(rocker_on == 0x651068D6);

  linapple_set_rocker_switch(false);
  CHECK_FALSE(linapple_get_rocker_switch());
  CHECK(frame_crc32() == rocker_off);
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
  const KeyboardHostKey host_a = {4, 'a', false, false};
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
