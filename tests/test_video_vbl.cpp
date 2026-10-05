// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <array>
#include <cstdint>
#include <string>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

// NTSC frame as the IIe's IOU counts it: 65 cycles a line, 262 lines, with
// lines 192-261 blanked, so blanking occupies frame cycles 12,480-17,029.
constexpr uint32_t cycles_per_line = 65;
constexpr uint32_t lines_per_frame = 262;
constexpr uint32_t frame_cycles = cycles_per_line * lines_per_frame;
constexpr uint32_t visible_lines = 192;
constexpr uint32_t blanking_start = visible_lines * cycles_per_line;

constexpr uint8_t bit_7 = 0x80;

constexpr uint16_t program_start = 0x0300;
constexpr uint16_t sample_table = 0x0400;

/**
 * @brief Mirror of the scanner's position within the frame.
 *
 * The scanner keeps that position in a static the core advances by the cycles
 * each cpu_execute ran. It starts at the top of the frame and nothing in this
 * binary moves it except this mirror, so a probe can be placed on an exact
 * frame cycle the way the core would reach it, and a stepped run is accounted
 * instruction by instruction as the core accounts a frame.
 */
struct FrameClock_t {
  uint32_t cycle = 0;

  auto advance_to(uint32_t target) -> void {
    const uint32_t delta = (target + frame_cycles - cycle) % frame_cycles;
    video_update_vbl(delta);
    cycle = target;
  }

  auto account(uint32_t executed) -> void {
    video_update_vbl(executed);
    cycle = (cycle + executed) % frame_cycles;
  }

  auto run_until(uint16_t sentinel, uint32_t cap) -> uint32_t {
    const CpuRegisters_t* regs = cpu_get_registers();
    uint32_t total = 0;
    while (regs->pc != sentinel && total < cap) {
      const uint32_t executed = cpu_execute(0);
      account(executed);
      total += executed;
    }
    return total;
  }
};

// LDA $C019, left for the 6502 to execute on the probed cycle.
constexpr std::array<uint8_t, 3> probe_program = {0xAD, 0x19, 0xC0};
constexpr uint16_t probe_end =
    static_cast<uint16_t>(program_start + probe_program.size());
constexpr uint32_t probe_cycles = 4;

// Samples $C019 once a line into $0400,X for 256 lines. Each pass is exactly
// 65 cycles -- LDA 4, STA abs,X 5, INX 2, BEQ not taken 2, LDY 2, eight DEY/BNE
// turns 39, four NOPs 8, JMP 3 -- so with X starting at zero the read for
// line L lands on frame cycle 65 * L, the line's first cycle. INX wraps X to
// zero after line 255 and the BEQ then leaves the loop.
constexpr std::array<uint8_t, 21> sampler_program = {
    0xAD, 0x19, 0xC0,        // LDA $C019
    0x9D, 0x00, 0x04,        // STA $0400,X
    0xE8,                    // INX
    0xF0, 0x0C,              // BEQ done
    0xA0, 0x08,              // LDY #$08
    0x88,                    // DEY
    0xD0, 0xFD,              // BNE *-1
    0xEA, 0xEA, 0xEA, 0xEA,  // NOP x4
    0x4C, 0x00, 0x03,        // JMP $0300
};
constexpr uint16_t sampler_done =
    static_cast<uint16_t>(program_start + sampler_program.size());
constexpr uint32_t sampled_lines = 256;
constexpr uint32_t sampler_exit_cycles = 4 + 5 + 2 + 3;
constexpr uint32_t sampler_cycles =
    ((sampled_lines - 1) * cycles_per_line) + sampler_exit_cycles;

struct Probe_t {
  uint32_t line;
  uint32_t cycle_in_line;
  const char* where;
};

constexpr std::array<Probe_t, 6> probes = {{
    {0, 0, "first cycle of the first visible line"},
    {96, 0, "middle of the visible area"},
    {visible_lines - 1, cycles_per_line - 1,
     "last cycle of the last visible line"},
    {visible_lines, 0, "first cycle of blanking"},
    {227, 0, "middle of blanking"},
    {lines_per_frame - 1, cycles_per_line - 1, "last cycle of the frame"},
}};

}  // namespace

TEST_CASE(
    "Video: $C019 reads high while the screen is drawn and low in blanking") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(system_state.video_scanner_ntsc);
  REQUIRE(system_state.clks_per_frame == frame_cycles);
  REQUIRE(blanking_start == 12480);
  REQUIRE(frame_cycles == 17030);

  FrameClock_t frame;

  // RDVBLBAR is VBL inverted, so bit 7 is set exactly on the visible lines.
  TestFixtures::ScopedCore_t::poke(program_start, probe_program);
  for (const Probe_t& probe : probes) {
    const uint32_t cycle = (probe.line * cycles_per_line) + probe.cycle_in_line;
    const bool visible = probe.line < visible_lines;
    INFO(std::string(probe.where));
    CAPTURE(cycle);
    frame.advance_to(cycle);
    TestFixtures::enter_at({program_start, 0, 0, 0});
    REQUIRE(frame.run_until(probe_end, probe_cycles * 2) == probe_cycles);
    CHECK(((cpu_get_registers()->a & bit_7) != 0) == visible);
  }

  // The same reading taken by a running program, one sample a line from the
  // top of the frame.
  TestFixtures::ScopedCore_t::poke(program_start, sampler_program);
  frame.advance_to(0);
  TestFixtures::enter_at({program_start, 0, 0, 0});
  REQUIRE(frame.run_until(sampler_done, sampler_cycles * 2) == sampler_cycles);
  REQUIRE(cpu_get_registers()->pc == sampler_done);
  for (uint32_t line = 0; line < sampled_lines; ++line) {
    CAPTURE(line);
    const uint8_t sample = mem[sample_table + line];
    CHECK(((sample & bit_7) != 0) == (line < visible_lines));
  }
}
