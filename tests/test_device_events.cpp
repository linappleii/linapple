// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "HeadlessHarness.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

#ifdef ENABLE_PERIPHERAL_DISK
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#endif

namespace {

using TestFixtures::ScopedCore_t;
using TestFixtures::ScopedTestConfig_t;

constexpr uint32_t frame_cycles = 17030;
// The longest 6502 instruction; an event is serviced at the first instruction
// boundary at or after its cycle, so this is the latest it may land when no
// interrupt is taken at that boundary.
constexpr uint64_t one_instruction = 7;
constexpr uint16_t program_start = 0x0300;
constexpr uint16_t spin_at = 0x030B;

struct Wake_t {
  uint64_t at;
  uint32_t cycles;
};

struct Read_t {
  uint32_t executed_cycles;
  uint64_t at;
  uint8_t bus;
};

// A card with nothing but a clock: it records when it is thought of and when
// its one register is read, and schedules whatever a case told it to.
struct Bench_t {
  HostInterface_t* host = nullptr;
  int slot = 0;

  uint64_t reset_offset = 0;
  bool cancel_after_reset = false;
  bool read_schedules = false;
  int64_t read_offset = 0;
  bool reschedule_now_from_think = false;
  bool keep_rescheduling = false;

  std::vector<Wake_t> wakes;
  std::vector<Read_t> reads;
  std::vector<uint64_t> targets;

  auto schedule(uint64_t at) -> void {
    targets.push_back(at);
    host->ScheduleEvent(this, at);
  }

  auto woken() const -> std::vector<Wake_t> {
    std::vector<Wake_t> out;
    for (const Wake_t& wake : wakes) {
      if (wake.cycles == 0) {
        out.push_back(wake);
      }
    }
    return out;
  }

  auto frame_ends() const -> std::vector<Wake_t> {
    std::vector<Wake_t> out;
    for (const Wake_t& wake : wakes) {
      if (wake.cycles != 0) {
        out.push_back(wake);
      }
    }
    return out;
  }
};

std::array<Bench_t, NUM_SLOTS> g_bench{};

auto bench_at(int slot) -> Bench_t& {
  return g_bench.at(static_cast<size_t>(slot));
}

auto bench_read_c0(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
                   uint8_t val, uint32_t executed_cycles) -> uint8_t {
  (void)pc;
  (void)val;
  auto* bench = static_cast<Bench_t*>(instance);
  const uint8_t bus = bench->host->ReadFloatingBus(executed_cycles);
  if (write != 0 || (addr & 0x0F) != 0) {
    return bus;
  }
  bench->reads.push_back({executed_cycles, bench->host->GetCycles(), bus});
  if (bench->read_schedules) {
    const auto now = static_cast<int64_t>(bench->host->GetCycles());
    bench->schedule(static_cast<uint64_t>(now + bench->read_offset));
  }
  return bus;
}

auto bench_init(int slot, HostInterface_t* host) -> void* {
  Bench_t& bench = bench_at(slot);
  bench.host = host;
  bench.slot = slot;
  host->RegisterIO(slot, bench_read_c0, bench_read_c0, nullptr, nullptr);
  return &bench;
}

auto bench_reset(void* instance) -> void {
  auto* bench = static_cast<Bench_t*>(instance);
  if (bench->keep_rescheduling) {
    bench->schedule(bench->host->GetCycles() + 1000);
    return;
  }
  if (bench->reset_offset != 0) {
    bench->schedule(bench->host->GetCycles() + bench->reset_offset);
  }
  if (bench->cancel_after_reset) {
    bench->host->ScheduleEvent(bench, 0);
  }
}

auto bench_shutdown(void* instance) -> void { (void)instance; }

auto bench_think(void* instance, uint32_t cycles) -> void {
  auto* bench = static_cast<Bench_t*>(instance);
  bench->wakes.push_back({bench->host->GetCycles(), cycles});
  if (cycles != 0) {
    return;
  }
  if (bench->reschedule_now_from_think) {
    bench->schedule(bench->host->GetCycles());
  }
  if (bench->keep_rescheduling) {
    bench->schedule(bench->host->GetCycles() + 1000);
  }
}

Peripheral_t g_bench_card = {LINAPPLE_ABI_VERSION,
                             "test.event_bench",
                             "EventBench",
                             "Asks to be woken at a cycle of its choosing",
                             "LinApple Contributors",
                             "1.0.0",
                             PERIPHERAL_MASK_EXPANSION,
                             -1,
                             bench_init,
                             bench_reset,
                             bench_shutdown,
                             bench_think,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr,
                             nullptr};

auto register_bench(int slot) -> Bench_t& {
  REQUIRE(peripheral_register(&g_bench_card, slot) == 0);
  return bench_at(slot);
}

auto c0_address(int slot) -> uint8_t {
  return static_cast<uint8_t>(0x80 + (slot << 4));
}

// LDA $C0n0 as the frame's first instruction (the handler sees offset 0),
// LDX #0, a 256-turn INX/BNE delay (2 + 255 x 3 + 256 x 2 - 1 = 1,279
// cycles, the last BNE not taken), a second LDA $C0n0 whose first cycle is
// offset 4 + 2 + 1,279 = 1,285, then a spin.
constexpr uint32_t second_read_offset = 1285;

auto poke_two_reads_then_spin(int slot) -> void {
  const uint8_t c0 = c0_address(slot);
  const std::array<uint8_t, 14> program = {
      0xAD, c0,   0xC0,  // LDA $C0n0
      0xA2, 0x00,        // LDX #$00
      0xE8,              // INX
      0xD0, 0xFD,        // BNE INX
      0xAD, c0,   0xC0,  // LDA $C0n0
      0x4C, 0x0B, 0x03   // JMP spin
  };
  ScopedCore_t::poke(program_start, program);
}

auto poke_spin() -> void {
  const std::array<uint8_t, 3> program = {0x4C, 0x00, 0x03};
  ScopedCore_t::poke(program_start, program);
}

// Interrupts masked, so no boundary carries the 7-cycle interrupt entry.
auto enter(uint16_t pc) -> void {
  CpuRegisters_t* regs = cpu_get_registers();
  regs->pc = pc;
  regs->ps |= 0x04;
}

auto within_one_instruction_after(uint64_t at, uint64_t target) -> bool {
  return at >= target && at - target <= one_instruction;
}

}  // namespace

TEST_CASE(
    "Device events: an event scheduled from reset wakes the card once at its "
    "cycle, and the frame-end think still comes once with the frame's total") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  constexpr int slot = 1;
  bench_at(slot) = Bench_t();
  bench_at(slot).reset_offset = 500;
  Bench_t& bench = register_bench(slot);
  harness.boot();
  poke_spin();
  enter(program_start);
  REQUIRE(bench.targets.size() == 1);
  const uint64_t target = bench.targets.at(0);
  CHECK(peripheral_next_event_cycle() == target);

  const uint32_t executed = linapple_run_frame(frame_cycles);
  CHECK(executed >= frame_cycles);
  CHECK(executed <= frame_cycles + one_instruction);

  const std::vector<Wake_t> woken = bench.woken();
  REQUIRE(woken.size() == 1);
  CHECK(within_one_instruction_after(woken.at(0).at, target));
  const std::vector<Wake_t> ends = bench.frame_ends();
  REQUIRE(ends.size() == 1);
  CHECK(ends.at(0).cycles == executed);
  CHECK(ends.at(0).at > woken.at(0).at);
  CHECK(peripheral_next_event_cycle() == UINT64_MAX);
}

TEST_CASE(
    "Device events: an event scheduled from inside a register access ends "
    "the running slice and is serviced within one instruction") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  constexpr int slot = 1;
  bench_at(slot) = Bench_t();
  bench_at(slot).read_schedules = true;
  bench_at(slot).read_offset = 300;
  Bench_t& bench = register_bench(slot);
  harness.boot();
  poke_two_reads_then_spin(slot);
  enter(program_start);

  const uint64_t frame_start = cpu_get_cumulative_cycles();
  harness.run_frames(1);

  REQUIRE(bench.reads.size() == 2);
  REQUIRE(bench.targets.size() == 2);
  const std::vector<Wake_t> woken = bench.woken();
  REQUIRE(woken.size() == 2);
  for (size_t i = 0; i < woken.size(); ++i) {
    CHECK(within_one_instruction_after(woken.at(i).at, bench.targets.at(i)));
    CHECK(woken.at(i).at < frame_start + frame_cycles);
  }
}

TEST_CASE(
    "Device events: two reads either side of an event see the offsets of one "
    "batch, so the slices continue the frame's count") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  constexpr int slot = 1;
  bench_at(slot) = Bench_t();
  bench_at(slot).reset_offset = 300;
  Bench_t& bench = register_bench(slot);
  harness.boot();
  poke_two_reads_then_spin(slot);
  enter(program_start);

  const uint64_t frame_start = cpu_get_cumulative_cycles();
  harness.run_frames(1);

  REQUIRE(bench.woken().size() == 1);
  REQUIRE(bench.reads.size() == 2);
  CHECK(bench.reads.at(0).executed_cycles == 0);
  CHECK(bench.reads.at(1).executed_cycles == second_read_offset);
  CHECK(bench.reads.at(0).at == frame_start);
  CHECK(bench.reads.at(1).at == frame_start + second_read_offset);
}

TEST_CASE("Device events: scheduling cycle 0 cancels and nothing wakes") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  constexpr int slot = 1;
  bench_at(slot) = Bench_t();
  bench_at(slot).reset_offset = 500;
  bench_at(slot).cancel_after_reset = true;
  Bench_t& bench = register_bench(slot);
  harness.boot();
  poke_spin();
  enter(program_start);
  CHECK(peripheral_next_event_cycle() == UINT64_MAX);

  harness.run_frames(1);
  CHECK(bench.woken().empty());
  CHECK(bench.frame_ends().size() == 1);
}

TEST_CASE(
    "Device events: a target behind the counter is serviced at the next "
    "boundary, one instruction on") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  constexpr int slot = 1;
  bench_at(slot) = Bench_t();
  Bench_t& bench = register_bench(slot);
  harness.boot();
  poke_two_reads_then_spin(slot);
  enter(program_start);
  // One frame first, so that a cycle 100 behind now exists.
  harness.run_frames(1);
  REQUIRE(bench.reads.size() == 2);
  bench.reads.clear();
  bench.wakes.clear();

  bench.read_schedules = true;
  bench.read_offset = -100;
  enter(program_start);
  harness.run_frames(1);

  REQUIRE(bench.reads.size() == 2);
  const std::vector<Wake_t> woken = bench.woken();
  REQUIRE(woken.size() == 2);
  for (size_t i = 0; i < woken.size(); ++i) {
    const uint64_t read_at = bench.reads.at(i).at;
    CHECK(woken.at(i).at > read_at);
    CHECK(woken.at(i).at - read_at <= one_instruction);
  }
}

TEST_CASE(
    "Device events: two cards in two slots are each woken at their own cycle, "
    "in order") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  bench_at(1) = Bench_t();
  bench_at(1).reset_offset = 500;
  bench_at(3) = Bench_t();
  bench_at(3).reset_offset = 900;
  Bench_t& first = register_bench(1);
  Bench_t& second = register_bench(3);
  harness.boot();
  poke_spin();
  enter(program_start);

  harness.run_frames(1);

  REQUIRE(first.woken().size() == 1);
  REQUIRE(second.woken().size() == 1);
  CHECK(within_one_instruction_after(first.woken().at(0).at,
                                     first.targets.at(0)));
  CHECK(within_one_instruction_after(second.woken().at(0).at,
                                     second.targets.at(0)));
  CHECK(first.woken().at(0).at < second.woken().at(0).at);
  CHECK(second.targets.at(0) - first.targets.at(0) == 400);
}

TEST_CASE(
    "Device events: a card that re-schedules now from its own wake is woken "
    "again after exactly one instruction, never twice in one pass, and the "
    "frame completes") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  constexpr int slot = 1;
  bench_at(slot) = Bench_t();
  bench_at(slot).reset_offset = 100;
  bench_at(slot).reschedule_now_from_think = true;
  Bench_t& bench = register_bench(slot);
  harness.boot();
  poke_spin();
  enter(program_start);

  const uint32_t executed = linapple_run_frame(frame_cycles);
  CHECK(executed >= frame_cycles);
  CHECK(executed <= frame_cycles + one_instruction);

  const std::vector<Wake_t> woken = bench.woken();
  REQUIRE(woken.size() > 100);
  for (size_t i = 1; i < woken.size(); ++i) {
    CHECK(woken.at(i).at > woken.at(i - 1).at);
    CHECK(woken.at(i).at - woken.at(i - 1).at <= one_instruction);
  }
  CHECK(bench.frame_ends().size() == 1);
}

TEST_CASE(
    "Device events: a frame with a pending event runs the frame's cycles plus "
    "at most one instruction, thinks once with that total, and reads the same "
    "scanner byte as a frame with none") {
  constexpr int slot = 1;
  std::array<std::vector<Read_t>, 2> reads{};
  for (size_t leg = 0; leg < 2; ++leg) {
    ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
    HeadlessHarness_t harness(config);
    bench_at(slot) = Bench_t();
    bench_at(slot).reset_offset = leg == 0 ? 0 : 500;
    Bench_t& bench = register_bench(slot);
    harness.boot();
    poke_two_reads_then_spin(slot);
    enter(program_start);

    const uint32_t executed = linapple_run_frame(frame_cycles);
    CHECK(executed >= frame_cycles);
    CHECK(executed <= frame_cycles + one_instruction);
    const std::vector<Wake_t> ends = bench.frame_ends();
    REQUIRE(ends.size() == 1);
    CHECK(ends.at(0).cycles == executed);
    CHECK(bench.woken().size() == leg);
    reads.at(leg) = bench.reads;
  }

  REQUIRE(reads.at(0).size() == 2);
  REQUIRE(reads.at(1).size() == 2);
  for (size_t i = 0; i < 2; ++i) {
    CHECK(reads.at(0).at(i).executed_cycles ==
          reads.at(1).at(i).executed_cycles);
    CHECK(reads.at(0).at(i).bus == reads.at(1).at(i).bus);
  }
}

TEST_CASE(
    "Device events: a direct cpu_execute runs to its count whatever is "
    "scheduled, and the event is serviced when the next frame begins") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  constexpr int slot = 1;
  bench_at(slot) = Bench_t();
  bench_at(slot).read_schedules = true;
  bench_at(slot).read_offset = 300;
  Bench_t& bench = register_bench(slot);
  harness.boot();
  poke_two_reads_then_spin(slot);
  enter(program_start);

  const uint32_t ran = cpu_execute(1000);
  CHECK(ran >= 1000);
  REQUIRE(bench.reads.size() == 1);
  REQUIRE(bench.targets.size() == 1);
  CHECK(bench.woken().empty());
  CHECK(peripheral_next_event_cycle() == bench.targets.at(0));

  const uint64_t before = cpu_get_cumulative_cycles();
  CHECK(before > bench.targets.at(0));
  harness.run_frames(1);
  const std::vector<Wake_t> woken = bench.woken();
  REQUIRE(woken.size() >= 1);
  CHECK(woken.at(0).at == before);
}

TEST_CASE(
    "Device events: a reset clears a pending event, and an event scheduled "
    "from reset survives it") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  constexpr int slot = 1;
  bench_at(slot) = Bench_t();
  Bench_t& bench = register_bench(slot);
  harness.boot();
  poke_spin();
  enter(program_start);

  bench.schedule(bench.host->GetCycles() + 500);
  CHECK(peripheral_next_event_cycle() == bench.targets.at(0));
  peripheral_manager_reset();
  CHECK(peripheral_next_event_cycle() == UINT64_MAX);
  harness.run_frames(1);
  CHECK(bench.woken().empty());

  bench.reset_offset = 500;
  peripheral_manager_reset();
  REQUIRE(bench.targets.size() == 2);
  CHECK(peripheral_next_event_cycle() == bench.targets.at(1));
  harness.run_frames(1);
  REQUIRE(bench.woken().size() == 1);
  CHECK(within_one_instruction_after(bench.woken().at(0).at,
                                     bench.targets.at(1)));
}

TEST_CASE(
    "Device events: unregistering the card drops its event and the next frame "
    "runs unsliced") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  constexpr int slot = 1;
  bench_at(slot) = Bench_t();
  bench_at(slot).reset_offset = 500;
  Bench_t& bench = register_bench(slot);
  harness.boot();
  poke_spin();
  enter(program_start);
  CHECK(peripheral_next_event_cycle() == bench.targets.at(0));

  REQUIRE(peripheral_unregister(slot) == 0);
  CHECK(peripheral_next_event_cycle() == UINT64_MAX);
  const uint32_t executed = linapple_run_frame(frame_cycles);
  CHECK(executed >= frame_cycles);
  CHECK(bench.wakes.empty());
}

TEST_CASE("Device events: cpu_execute(0) still runs exactly one instruction") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  harness.boot();
  const std::array<uint8_t, 2> nops = {0xEA, 0xEA};
  ScopedCore_t::poke(program_start, nops);
  enter(program_start);

  CHECK(cpu_execute(0) == 2);
  CHECK(cpu_get_registers()->pc == program_start + 1);
}

#ifdef ENABLE_PERIPHERAL_DISK
namespace {

struct BootRecord_t {
  int prompt_frame = -1;
  std::array<std::string, 24> rows;
  bool drive_error = false;
};

// Boots DOS 3.3 from the master disk with a bench card in slot 1 that wakes
// every 1,000 cycles when asked to, and records the frame at which the
// prompt appears and the screen at that frame. The Disk II never schedules,
// so with the bench quiet the frame runs as one slice.
auto boot_master_disk(bool bench_schedules) -> BootRecord_t {
  ScopedTestConfig_t::Description_t description;
  description.slots[5] = "Disk II";
  description.extras.push_back({"Configuration", "Disk Turbo", "0"});
  ScopedTestConfig_t config(description);
  HeadlessHarness_t harness(config);
  bench_at(1) = Bench_t();
  bench_at(1).keep_rescheduling = bench_schedules;
  Bench_t& bench = register_bench(1);
  auto disk = TestFixtures::create_ephemeral("Master.dsk");
  harness.mount_disk(6, 0, disk);
  harness.boot();

  BootRecord_t record;
  for (int frame = 0; frame < 400 && record.prompt_frame < 0; ++frame) {
    harness.run_frames(1);
    for (int row = 0; row < 24; ++row) {
      record.rows.at(static_cast<size_t>(row)) = harness.get_text_row(row);
    }
    for (const std::string& row : record.rows) {
      if (row.find(']') != std::string::npos) {
        record.prompt_frame = frame;
        break;
      }
    }
  }

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_query_status, &status, &size);
  record.drive_error = status.drive0_last_error != disk_err_none;
  CHECK(bench.woken().empty() == !bench_schedules);
  return record;
}

}  // namespace

TEST_CASE(
    "Device events: the Disk II boots DOS 3.3 to the same screen at the same "
    "frame whether or not a card slices the frames") {
  const BootRecord_t quiet = boot_master_disk(false);
  const BootRecord_t sliced = boot_master_disk(true);
  REQUIRE(quiet.prompt_frame >= 0);
  CHECK(sliced.prompt_frame == quiet.prompt_frame);
  CHECK(sliced.rows == quiet.rows);
  CHECK_FALSE(quiet.drive_error);
  CHECK_FALSE(sliced.drive_error);
}
#endif

#ifdef ENABLE_PERIPHERAL_MOCKINGBOARD
namespace {

constexpr uint16_t t1_table = 0x0600;

// Sets the 6522's T1 free-running from $FFFF and copies T1C-L into a table
// every 64 loop turns: the counter is the value the card's sync advances by
// the batch's executed count, so a slice that restarted the count would show
// as a wrong entry.
auto poke_t1_sampler() -> void {
  const std::array<uint8_t, 32> program = {
      0xA9, 0x40,        // LDA #$40
      0x8D, 0x0B, 0xC4,  // STA $C40B   ACR: T1 free-running
      0xA9, 0xFF,        // LDA #$FF
      0x8D, 0x04, 0xC4,  // STA $C404   T1 latch low
      0x8D, 0x05, 0xC4,  // STA $C405   T1 latch high, load and start
      0xA2, 0x00,        // LDX #$00
      0xA0, 0x40,        // LDY #$40    loop:
      0x88,              // DEY         turn:
      0xD0, 0xFD,        // BNE turn
      0xAD, 0x04, 0xC4,  // LDA $C404   T1C-L
      0x9D, 0x00, 0x06,  // STA $0600,X
      0xE8,              // INX
      0xD0, 0xF2,        // BNE loop
      0x4C, 0x1D, 0x03   // JMP self (table full)
  };
  ScopedCore_t::poke(program_start, program);
}

auto sample_t1(bool bench_schedules) -> std::array<uint8_t, 256> {
  ScopedTestConfig_t::Description_t description;
  description.slots[3] = "Mockingboard";
  ScopedTestConfig_t config(description);
  HeadlessHarness_t harness(config);
  bench_at(1) = Bench_t();
  bench_at(1).keep_rescheduling = bench_schedules;
  Bench_t& bench = register_bench(1);
  harness.boot();
  poke_t1_sampler();
  enter(program_start);
  harness.run_frames(2);

  std::array<uint8_t, 256> table{};
  for (size_t i = 0; i < table.size(); ++i) {
    table.at(i) = mem[t1_table + i];
  }
  CHECK(bench.woken().empty() == !bench_schedules);
  return table;
}

}  // namespace

TEST_CASE(
    "Device events: the Mockingboard's T1 counts the same whether or not a "
    "card slices the frames") {
  const std::array<uint8_t, 256> quiet = sample_t1(false);
  const std::array<uint8_t, 256> sliced = sample_t1(true);
  CHECK(sliced == quiet);
  // The timer ran: the sampled low byte moved between entries.
  CHECK(quiet.at(0) != quiet.at(1));
  CHECK(quiet.at(1) != quiet.at(2));
}
#endif
