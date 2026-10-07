// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"
#if ENABLE_DEBUGGER
#include "Debugger/Debugger_Assembler.h"
#endif

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr size_t block_size = 512;
constexpr size_t page_size = 256;
constexpr uint16_t program_start = 0x0300;
constexpr uint16_t results_at = 0x1000;
constexpr uint16_t page_copy_at = 0x1100;
constexpr uint16_t read_buffer = 0x2000;
constexpr uint16_t write_buffer = 0x2400;
constexpr uint16_t boot_buffer = 0x0800;
constexpr uint16_t boot_entry = 0x0801;
constexpr uint8_t driver_offset = 0x46;
constexpr uint8_t trampoline_offset = 0x3F;
constexpr uint8_t fail_offset = 0xAF;
constexpr uint8_t ret1_offset = 0x1E;
constexpr uint8_t ret2_offset = 0x38;
constexpr uint16_t monitor_sloop = 0xFABA;
constexpr uint16_t monitor_no_slot = 0xFA9B;
constexpr uint16_t monitor_reset_entry = 0xFF59;
constexpr uint16_t basic_cold_start = 0xE000;
constexpr uint32_t call_cycle_cap = 20000;
constexpr uint32_t copy_cycle_cap = 10000;
constexpr uint32_t boot_cycle_cap = 2000000;
constexpr uint32_t frame_cycles = 17030;
constexpr uint32_t boot_frame_cap = 300;

// ProDOS 8 Technical Reference Manual, 6.3.2.
constexpr uint8_t prodos_status = 0x00;
constexpr uint8_t prodos_read = 0x01;
constexpr uint8_t prodos_write = 0x02;
constexpr uint8_t prodos_format = 0x03;
constexpr uint8_t prodos_ok = 0x00;
constexpr uint8_t prodos_io_error = 0x27;
constexpr uint8_t prodos_no_device = 0x28;
constexpr uint8_t prodos_write_protected = 0x2B;
constexpr uint8_t unit_drive_2 = 0x80;
constexpr uint8_t status_carry = 0x01;

constexpr uint16_t hdv_blocks = 16;
constexpr uint16_t po_blocks = 280;

auto slot_page(int slot) -> uint16_t {
  return static_cast<uint16_t>(0xC000 + (slot << 8));
}

auto io_base(int slot) -> uint16_t {
  return static_cast<uint16_t>(0xC080 + (slot << 4));
}

auto unit_for(int slot, int drive) -> uint8_t {
  return static_cast<uint8_t>((slot << 4) | (drive != 0 ? unit_drive_2 : 0));
}

auto machine_with_card(int slot) -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots.at(static_cast<size_t>(slot - 1)) = "Harddisk";
  return description;
}

// The bridge alone, as a saved session resumes: the manager built, the cards
// registered from the slot table and the machine at power-on.
struct Machine_t {
  TestConfig_t config;
  TestFixtures::ScopedCore_t core;

  explicit Machine_t(const TestConfig_t::Description_t& description)
      : config(description), core(config) {
    peripheral_manager_init();
    linapple_register_peripherals();
    linapple_reset_hard();
  }
};

auto settle() -> void { peripheral_manager_think(0); }

auto insert(int slot, int drive, const std::string& path,
            bool write_protected = false) -> void {
  HarddiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  cmd.write_protected = write_protected ? 1 : 0;
  std::strncpy(cmd.path, path.c_str(), sizeof(cmd.path) - 1);
  REQUIRE(peripheral_command(slot, harddisk_cmd_insert, &cmd, sizeof(cmd)) ==
          peripheral_ok);
  settle();
}

auto eject(int slot, int drive) -> void {
  HarddiskEjectCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  REQUIRE(peripheral_command(slot, harddisk_cmd_eject, &cmd, sizeof(cmd)) ==
          peripheral_ok);
  settle();
}

auto set_protect(int slot, int drive, bool protect) -> void {
  HarddiskSetProtectCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  cmd.write_protected = protect ? 1 : 0;
  REQUIRE(peripheral_command(slot, harddisk_cmd_set_protect, &cmd,
                             sizeof(cmd)) == peripheral_ok);
  settle();
}

auto status(int slot) -> HarddiskStatus_t {
  HarddiskStatus_t out{};
  size_t size = sizeof(out);
  REQUIRE(peripheral_query(slot, harddisk_query_status, &out, &size) ==
          peripheral_ok);
  return out;
}

auto peek_io(uint16_t addr) -> uint8_t {
  return io_map_dispatch(0, addr, 0, 0, 0);
}

auto poke_io(uint16_t addr, uint8_t value) -> void {
  io_map_dispatch(0, addr, 1, value, 0);
}

auto fill(uint16_t addr, size_t count, uint8_t value) -> void {
  std::vector<uint8_t> bytes(count, value);
  TestFixtures::ScopedCore_t::poke(addr, bytes.data(), bytes.size());
}

auto read_block_from_file(const std::string& path, uint32_t block)
    -> std::array<uint8_t, block_size> {
  std::array<uint8_t, block_size> bytes{};
  FilePtr_t file{fopen(path.c_str(), "rb"), fclose};
  REQUIRE(file != nullptr);
  REQUIRE(fseek(file.get(), static_cast<long>(block * block_size), SEEK_SET) ==
          0);
  REQUIRE(fread(bytes.data(), 1, bytes.size(), file.get()) == bytes.size());
  return bytes;
}

auto read_whole_file(const std::string& path) -> std::vector<uint8_t> {
  FilePtr_t file{fopen(path.c_str(), "rb"), fclose};
  REQUIRE(file != nullptr);
  const int64_t size = Path::file_size(file.get());
  REQUIRE(size >= 0);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  REQUIRE(fread(bytes.data(), 1, bytes.size(), file.get()) == bytes.size());
  return bytes;
}

// Every instruction the 6502 fetches, until the sentinel or the cap; what a
// boot passed through is as much the oracle as where it ended.
auto run_recording(uint16_t sentinel, uint32_t cap) -> std::vector<uint16_t> {
  std::vector<uint16_t> visited;
  const CpuRegisters_t* regs = cpu_get_registers();
  uint32_t cycles = 0;
  while (regs->pc != sentinel && cycles < cap) {
    visited.push_back(regs->pc);
    cycles += cpu_execute(0);
  }
  return visited;
}

// Leaves the sentinel an earlier run stopped on, so the next run can look for
// a later visit to it.
auto step_once() -> void { cpu_execute(0); }

auto visited(const std::vector<uint16_t>& trace, uint16_t pc) -> bool {
  return std::find(trace.begin(), trace.end(), pc) != trace.end();
}

// True when the addresses were passed in the order given, not necessarily
// consecutively.
auto visited_in_order(const std::vector<uint16_t>& trace,
                      const std::vector<uint16_t>& sequence) -> bool {
  size_t next = 0;
  for (const uint16_t pc : trace) {
    if (next < sequence.size() && pc == sequence.at(next)) {
      ++next;
    }
  }
  return next == sequence.size();
}

// Copies the card's page into RAM through the 6502, which is the only view
// the firmware has of itself.
auto read_slot_page(int slot) -> std::array<uint8_t, page_size> {
  const uint16_t page = slot_page(slot);
  // LDY #0 / LDA $Cn00,Y / STA copy,Y / INY / BNE / JMP spin
  const std::array<uint8_t, 13> program = {
      0xA0,
      0x00,
      0xB9,
      static_cast<uint8_t>(page & 0xFF),
      static_cast<uint8_t>(page >> 8),
      0x99,
      static_cast<uint8_t>(page_copy_at & 0xFF),
      static_cast<uint8_t>(page_copy_at >> 8),
      0xC8,
      0xD0,
      0xF7,
      0x4C,
      0x0B};
  std::array<uint8_t, 14> with_spin{};
  std::copy(program.begin(), program.end(), with_spin.begin());
  with_spin.at(13) = static_cast<uint8_t>(program_start >> 8);
  const uint16_t sentinel = program_start + 11;
  TestFixtures::ScopedCore_t::poke(program_start, with_spin);
  TestFixtures::enter_at({program_start, 0, 0, 0});
  TestFixtures::step_until_pc(sentinel, copy_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == sentinel);

  std::array<uint8_t, page_size> copy{};
  for (size_t i = 0; i < page_size; ++i) {
    copy.at(i) = mem[page_copy_at + i];
  }
  return copy;
}

struct Call_t {
  uint8_t command;
  uint8_t unit;
  uint16_t buffer;
  uint16_t block;
};

struct CallResult_t {
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t p;

  auto carry() const -> bool { return (p & status_carry) != 0; }
};

// The exerciser: the parameter block as the MLI sets it, a JSR to the entry,
// and A, X, Y and P stored where the test can read them.
auto call_driver(uint16_t entry, const Call_t& call) -> CallResult_t {
  const std::array<uint8_t, 44> program = {
      0xA9,
      call.command,
      0x85,
      0x42,
      0xA9,
      call.unit,
      0x85,
      0x43,
      0xA9,
      static_cast<uint8_t>(call.buffer & 0xFF),
      0x85,
      0x44,
      0xA9,
      static_cast<uint8_t>(call.buffer >> 8),
      0x85,
      0x45,
      0xA9,
      static_cast<uint8_t>(call.block & 0xFF),
      0x85,
      0x46,
      0xA9,
      static_cast<uint8_t>(call.block >> 8),
      0x85,
      0x47,
      0x20,
      static_cast<uint8_t>(entry & 0xFF),
      static_cast<uint8_t>(entry >> 8),
      0x8D,
      static_cast<uint8_t>(results_at & 0xFF),
      static_cast<uint8_t>(results_at >> 8),
      0x8E,
      static_cast<uint8_t>((results_at + 1) & 0xFF),
      static_cast<uint8_t>(results_at >> 8),
      0x8C,
      static_cast<uint8_t>((results_at + 2) & 0xFF),
      static_cast<uint8_t>(results_at >> 8),
      0x08,
      0x68,
      0x8D,
      static_cast<uint8_t>((results_at + 3) & 0xFF),
      static_cast<uint8_t>(results_at >> 8),
      0x4C,
      static_cast<uint8_t>((program_start + 41) & 0xFF),
      static_cast<uint8_t>((program_start + 41) >> 8)};
  const uint16_t sentinel = program_start + 41;
  TestFixtures::ScopedCore_t::poke(program_start, program);
  TestFixtures::enter_at({program_start, 0, 0, 0});
  TestFixtures::step_until_pc(sentinel, call_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == sentinel);
  return CallResult_t{mem[results_at], mem[results_at + 1], mem[results_at + 2],
                      mem[results_at + 3]};
}

auto call_card(int slot, const Call_t& call) -> CallResult_t {
  return call_driver(static_cast<uint16_t>(slot_page(slot) + driver_offset),
                     call);
}

auto parameter_block() -> std::array<uint8_t, 6> {
  std::array<uint8_t, 6> bytes{};
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes.at(i) = mem[0x42 + i];
  }
  return bytes;
}

auto buffer_all(uint16_t addr, uint8_t value) -> bool {
  for (size_t i = 0; i < block_size; ++i) {
    if (mem[addr + i] != value) {
      return false;
    }
  }
  return true;
}

// The boots that end at a DOS prompt need the Disk II, as their cases do.
#if defined(ENABLE_PERIPHERAL_DISK)
auto text_row_begins_with(char first) -> bool {
  static const std::array<uint16_t, 24> row_offsets = {
      0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
      0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0};
  for (const uint16_t offset : row_offsets) {
    if (static_cast<char>(mem[0x0400 + offset] & 0x7F) == first) {
      return true;
    }
  }
  return false;
}

auto run_frames_until_prompt(uint32_t cap) -> bool {
  system_state.mode = app_mode_running;
  for (uint32_t frame = 0; frame < cap; ++frame) {
    linapple_run_frame(frame_cycles);
    if (text_row_begins_with(']')) {
      return true;
    }
  }
  return false;
}
#endif

// --- The listing, typed in ---------------------------------------------

enum Mnemonic_e {
  m_lda,
  m_sta,
  m_ldx,
  m_ldy,
  m_tax,
  m_pha,
  m_iny,
  m_inc,
  m_dec,
  m_and,
  m_ora,
  m_cmp,
  m_asl,
  m_lsr,
  m_beq,
  m_bne,
  m_bcs,
  m_jmp,
  m_clc,
  m_sec,
  m_rts
};

enum Mode_e {
  mode_implied,
  mode_accumulator,
  mode_immediate,
  mode_zero_page,
  mode_absolute,
  mode_absolute_x,
  mode_indirect_y,
  mode_relative
};

struct Opcode_t {
  Mnemonic_e mnemonic;
  Mode_e mode;
  uint8_t opcode;
};

// The 28 (mnemonic, mode) pairs the listing uses, from the 6502 opcode map.
constexpr std::array<Opcode_t, 28> k_opcodes = {{
    {m_lda, mode_immediate, 0xA9},   {m_lda, mode_zero_page, 0xA5},
    {m_lda, mode_absolute, 0xAD},    {m_lda, mode_absolute_x, 0xBD},
    {m_lda, mode_indirect_y, 0xB1},  {m_sta, mode_zero_page, 0x85},
    {m_sta, mode_absolute_x, 0x9D},  {m_sta, mode_indirect_y, 0x91},
    {m_ldx, mode_zero_page, 0xA6},   {m_ldy, mode_immediate, 0xA0},
    {m_ldy, mode_absolute_x, 0xBC},  {m_tax, mode_implied, 0xAA},
    {m_pha, mode_implied, 0x48},     {m_iny, mode_implied, 0xC8},
    {m_inc, mode_zero_page, 0xE6},   {m_dec, mode_zero_page, 0xC6},
    {m_and, mode_immediate, 0x29},   {m_ora, mode_immediate, 0x09},
    {m_cmp, mode_immediate, 0xC9},   {m_asl, mode_accumulator, 0x0A},
    {m_lsr, mode_accumulator, 0x4A}, {m_beq, mode_relative, 0xF0},
    {m_bne, mode_relative, 0xD0},    {m_bcs, mode_relative, 0xB0},
    {m_jmp, mode_absolute, 0x4C},    {m_clc, mode_implied, 0x18},
    {m_sec, mode_implied, 0x38},     {m_rts, mode_implied, 0x60},
}};

auto mnemonic_name(Mnemonic_e mnemonic) -> const char* {
  switch (mnemonic) {
    case m_lda:
      return "LDA";
    case m_sta:
      return "STA";
    case m_ldx:
      return "LDX";
    case m_ldy:
      return "LDY";
    case m_tax:
      return "TAX";
    case m_pha:
      return "PHA";
    case m_iny:
      return "INY";
    case m_inc:
      return "INC";
    case m_dec:
      return "DEC";
    case m_and:
      return "AND";
    case m_ora:
      return "ORA";
    case m_cmp:
      return "CMP";
    case m_asl:
      return "ASL";
    case m_lsr:
      return "LSR";
    case m_beq:
      return "BEQ";
    case m_bne:
      return "BNE";
    case m_bcs:
      return "BCS";
    case m_jmp:
      return "JMP";
    case m_clc:
      return "CLC";
    case m_sec:
      return "SEC";
    case m_rts:
      return "RTS";
  }
  return "???";
}

auto instruction_length(Mode_e mode) -> uint8_t {
  switch (mode) {
    case mode_implied:
    case mode_accumulator:
      return 1;
    case mode_absolute:
    case mode_absolute_x:
      return 3;
    default:
      return 2;
  }
}

struct Row_t {
  uint8_t offset;
  Mnemonic_e mnemonic;
  Mode_e mode;
  // The immediate or zero-page byte, the absolute address, or for a branch
  // the target's offset in the page.
  uint16_t operand;
};

// Labels: RET1 $1E, RET2 $38, FAILJ $3F, DRIVER $46, W1 $62, W2 $6C, EXEC
// $78, R1 $8A, R2 $94, DONE $9E, STATUS $A2, ERROR $AD, FAIL $AF, MONITOR $C7.
const std::vector<Row_t> k_listing = {
    {0x00, m_lda, mode_immediate, 0x20},
    {0x02, m_lda, mode_immediate, 0x00},
    {0x04, m_lda, mode_immediate, 0x03},
    {0x06, m_lda, mode_immediate, 0x3C},
    {0x08, m_lda, mode_immediate, 0xC0},
    {0x0A, m_sta, mode_zero_page, 0x47},
    {0x0C, m_asl, mode_accumulator, 0},
    {0x0D, m_asl, mode_accumulator, 0},
    {0x0E, m_asl, mode_accumulator, 0},
    {0x0F, m_asl, mode_accumulator, 0},
    {0x10, m_sta, mode_zero_page, 0x43},
    {0x12, m_lda, mode_zero_page, 0x47},
    {0x14, m_pha, mode_implied, 0},
    {0x15, m_lda, mode_immediate, 0x1D},
    {0x17, m_pha, mode_implied, 0},
    {0x18, m_lda, mode_immediate, 0x00},
    {0x1A, m_sta, mode_zero_page, 0x42},
    {0x1C, m_beq, mode_relative, 0x46},
    {0x1E, m_bcs, mode_relative, 0x3F},
    {0x20, m_lda, mode_zero_page, 0x47},
    {0x22, m_pha, mode_implied, 0},
    {0x23, m_lda, mode_immediate, 0x37},
    {0x25, m_pha, mode_implied, 0},
    {0x26, m_lda, mode_immediate, 0x00},
    {0x28, m_sta, mode_zero_page, 0x44},
    {0x2A, m_sta, mode_zero_page, 0x46},
    {0x2C, m_sta, mode_zero_page, 0x47},
    {0x2E, m_lda, mode_immediate, 0x08},
    {0x30, m_sta, mode_zero_page, 0x45},
    {0x32, m_lda, mode_immediate, 0x01},
    {0x34, m_sta, mode_zero_page, 0x42},
    {0x36, m_bne, mode_relative, 0x46},
    {0x38, m_bcs, mode_relative, 0x3F},
    {0x3A, m_ldx, mode_zero_page, 0x43},
    {0x3C, m_jmp, mode_absolute, 0x0801},
    {0x3F, m_bcs, mode_relative, 0xAF},
    {0x46, m_lda, mode_zero_page, 0x43},
    {0x48, m_and, mode_immediate, 0x70},
    {0x4A, m_tax, mode_implied, 0},
    {0x4B, m_lda, mode_zero_page, 0x43},
    {0x4D, m_sta, mode_absolute_x, 0xC081},
    {0x50, m_lda, mode_zero_page, 0x46},
    {0x52, m_sta, mode_absolute_x, 0xC082},
    {0x55, m_lda, mode_zero_page, 0x47},
    {0x57, m_sta, mode_absolute_x, 0xC083},
    {0x5A, m_lda, mode_zero_page, 0x42},
    {0x5C, m_cmp, mode_immediate, 0x02},
    {0x5E, m_bne, mode_relative, 0x78},
    {0x60, m_ldy, mode_immediate, 0x00},
    {0x62, m_lda, mode_indirect_y, 0x44},
    {0x64, m_sta, mode_absolute_x, 0xC084},
    {0x67, m_iny, mode_implied, 0},
    {0x68, m_bne, mode_relative, 0x62},
    {0x6A, m_inc, mode_zero_page, 0x45},
    {0x6C, m_lda, mode_indirect_y, 0x44},
    {0x6E, m_sta, mode_absolute_x, 0xC084},
    {0x71, m_iny, mode_implied, 0},
    {0x72, m_bne, mode_relative, 0x6C},
    {0x74, m_dec, mode_zero_page, 0x45},
    {0x76, m_lda, mode_zero_page, 0x42},
    {0x78, m_sta, mode_absolute_x, 0xC080},
    {0x7B, m_lda, mode_absolute_x, 0xC080},
    {0x7E, m_bne, mode_relative, 0xAD},
    {0x80, m_lda, mode_zero_page, 0x42},
    {0x82, m_beq, mode_relative, 0xA2},
    {0x84, m_cmp, mode_immediate, 0x01},
    {0x86, m_bne, mode_relative, 0x9E},
    {0x88, m_ldy, mode_immediate, 0x00},
    {0x8A, m_lda, mode_absolute_x, 0xC084},
    {0x8D, m_sta, mode_indirect_y, 0x44},
    {0x8F, m_iny, mode_implied, 0},
    {0x90, m_bne, mode_relative, 0x8A},
    {0x92, m_inc, mode_zero_page, 0x45},
    {0x94, m_lda, mode_absolute_x, 0xC084},
    {0x97, m_sta, mode_indirect_y, 0x44},
    {0x99, m_iny, mode_implied, 0},
    {0x9A, m_bne, mode_relative, 0x94},
    {0x9C, m_dec, mode_zero_page, 0x45},
    {0x9E, m_lda, mode_immediate, 0x00},
    {0xA0, m_clc, mode_implied, 0},
    {0xA1, m_rts, mode_implied, 0},
    {0xA2, m_ldy, mode_absolute_x, 0xC086},
    {0xA5, m_lda, mode_absolute_x, 0xC085},
    {0xA8, m_tax, mode_implied, 0},
    {0xA9, m_lda, mode_immediate, 0x00},
    {0xAB, m_clc, mode_implied, 0},
    {0xAC, m_rts, mode_implied, 0},
    {0xAD, m_sec, mode_implied, 0},
    {0xAE, m_rts, mode_implied, 0},
    {0xAF, m_lda, mode_absolute, 0xFBB3},
    {0xB2, m_cmp, mode_immediate, 0x38},
    {0xB4, m_beq, mode_relative, 0xC7},
    {0xB6, m_lda, mode_zero_page, 0x43},
    {0xB8, m_lsr, mode_accumulator, 0},
    {0xB9, m_lsr, mode_accumulator, 0},
    {0xBA, m_lsr, mode_accumulator, 0},
    {0xBB, m_lsr, mode_accumulator, 0},
    {0xBC, m_ora, mode_immediate, 0xC0},
    {0xBE, m_sta, mode_zero_page, 0x01},
    {0xC0, m_lda, mode_immediate, 0x00},
    {0xC2, m_sta, mode_zero_page, 0x00},
    {0xC4, m_jmp, mode_absolute, 0xFABA},
    {0xC7, m_jmp, mode_absolute, 0xFF59},
};

// The five unused bytes after the trampoline and the run from $CA to $FB.
constexpr uint8_t unused_after_trampoline = 0x41;
constexpr uint8_t code_end = 0xCA;
constexpr uint8_t tail_blocks_low = 0xFC;
constexpr uint8_t tail_status = 0xFE;
constexpr uint8_t tail_entry = 0xFF;
constexpr uint8_t status_byte = 0xDF;

auto opcode_for(Mnemonic_e mnemonic, Mode_e mode) -> uint8_t {
  for (const Opcode_t& entry : k_opcodes) {
    if (entry.mnemonic == mnemonic && entry.mode == mode) {
      return entry.opcode;
    }
  }
  FAIL("no opcode for " << mnemonic_name(mnemonic) << " in mode " << mode);
  return 0;
}

// Assembles the rows into a page: each row where its offset says, every
// branch's displacement from the row addresses, the tail bytes last. Rows
// must abut except across the two gaps.
auto assemble_listing() -> std::array<uint8_t, page_size> {
  std::array<uint8_t, page_size> page{};
  uint16_t expected_offset = 0;
  for (const Row_t& row : k_listing) {
    if (row.offset == driver_offset) {
      CHECK(expected_offset == unused_after_trampoline);
    } else {
      CHECK(row.offset == expected_offset);
    }
    const uint8_t length = instruction_length(row.mode);
    page.at(row.offset) = opcode_for(row.mnemonic, row.mode);
    if (row.mode == mode_relative) {
      const int displacement =
          static_cast<int>(row.operand) - (static_cast<int>(row.offset) + 2);
      REQUIRE(displacement >= -128);
      REQUIRE(displacement <= 127);
      page.at(row.offset + 1) = static_cast<uint8_t>(displacement & 0xFF);
    } else if (length == 2) {
      page.at(row.offset + 1) = static_cast<uint8_t>(row.operand & 0xFF);
    } else if (length == 3) {
      page.at(row.offset + 1) = static_cast<uint8_t>(row.operand & 0xFF);
      page.at(row.offset + 2) = static_cast<uint8_t>(row.operand >> 8);
    }
    expected_offset = static_cast<uint16_t>(row.offset + length);
  }
  CHECK(expected_offset == code_end);
  page.at(tail_blocks_low) = 0x00;
  page.at(tail_blocks_low + 1) = 0x00;
  page.at(tail_status) = status_byte;
  page.at(tail_entry) = driver_offset;
  return page;
}

}  // namespace

TEST_CASE(
    "Harddisk firmware: the listing assembled by its own opcode table is the "
    "page the card shows, byte for byte, with the slot patched into $09") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));

  std::array<uint8_t, page_size> expected = assemble_listing();
  expected.at(0x09) = static_cast<uint8_t>(0xC0 | slot);
  const std::array<uint8_t, page_size> page = read_slot_page(slot);

  for (size_t i = 0; i < page_size; ++i) {
    CAPTURE(i);
    CHECK(page.at(i) == expected.at(i));
  }
  for (size_t i = code_end; i < tail_blocks_low; ++i) {
    CAPTURE(i);
    CHECK(page.at(i) == 0);
  }
  CHECK(page.at(0xFC) == 0x00);
  CHECK(page.at(0xFD) == 0x00);
  CHECK(page.at(0xFE) == 0xDF);
  CHECK(page.at(0xFF) == 0x46);

#if ENABLE_DEBUGGER
  // A second opinion on every opcode byte: the debugger's own 6502 table
  // must read each row back as the mnemonic and mode the listing names.
  for (const Row_t& row : k_listing) {
    CAPTURE(row.offset);
    const Opcodes_t& opcode = g_opcodes6502[page.at(row.offset)];
    CHECK(std::string(opcode.sMnemonic) == mnemonic_name(row.mnemonic));
    int debugger_mode = AM_IMPLIED;
    switch (row.mode) {
      case mode_immediate:
        debugger_mode = AM_M;
        break;
      case mode_zero_page:
        debugger_mode = AM_Z;
        break;
      case mode_absolute:
        debugger_mode = AM_A;
        break;
      case mode_absolute_x:
        debugger_mode = AM_AX;
        break;
      case mode_indirect_y:
        debugger_mode = AM_NZY;
        break;
      case mode_relative:
        debugger_mode = AM_R;
        break;
      case mode_implied:
      case mode_accumulator:
        break;
    }
    CHECK(opcode.nAddressMode == debugger_mode);
  }
#endif
}

TEST_CASE(
    "Harddisk firmware: the ProDOS block-device ID bytes, the Disk II fourth "
    "byte, a zero block count, the status byte and the driver entry read "
    "through the bus in slots 7, 5 and 1") {
  for (const int slot : {7, 5, 1}) {
    CAPTURE(slot);
    Machine_t machine(machine_with_card(slot));
    REQUIRE(peripheral_present(slot, harddisk_id));
    const std::array<uint8_t, page_size> page = read_slot_page(slot);

    // ProDOS 8 Technical Reference Manual, 6.3.1; Technical Note #21 for
    // $Cn07.
    CHECK(page.at(0x01) == 0x20);
    CHECK(page.at(0x03) == 0x00);
    CHECK(page.at(0x05) == 0x03);
    CHECK(page.at(0x07) == 0x3C);
    CHECK(page.at(0xFC) == 0x00);
    CHECK(page.at(0xFD) == 0x00);
    CHECK(page.at(0xFE) == 0xDF);
    CHECK(page.at(0xFF) == 0x46);
    CHECK(page.at(0x09) == static_cast<uint8_t>(0xC0 | slot));
  }
}

TEST_CASE(
    "Harddisk firmware: a hard reset with an image boots block 0 to $0800 "
    "through two driver calls and enters $0801 with X the slot times 16") {
  for (const int slot : {7, 5}) {
    CAPTURE(slot);
    Machine_t machine(machine_with_card(slot));
    REQUIRE(peripheral_present(slot, harddisk_id));
    const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
    insert(slot, 0, image.path());
    const std::array<uint8_t, block_size> block0 =
        read_block_from_file(image.path(), 0);

    linapple_reset_hard();
    const std::vector<uint16_t> trace =
        run_recording(boot_entry, boot_cycle_cap);
    REQUIRE(cpu_get_registers()->pc == boot_entry);

    const uint16_t page = slot_page(slot);
    CHECK(visited(trace, page));
    CHECK(visited_in_order(trace, {static_cast<uint16_t>(page + driver_offset),
                                   static_cast<uint16_t>(page + ret1_offset),
                                   static_cast<uint16_t>(page + driver_offset),
                                   static_cast<uint16_t>(page + ret2_offset)}));
    CHECK_FALSE(visited(trace, static_cast<uint16_t>(page + fail_offset)));

    CHECK(cpu_get_registers()->x == static_cast<uint8_t>(slot << 4));
    for (size_t i = 0; i < block_size; ++i) {
      CAPTURE(i);
      CHECK(mem[boot_buffer + i] == block0.at(i));
    }
    const std::array<uint8_t, 6> params = parameter_block();
    CHECK(params.at(0) == prodos_read);
    CHECK(params.at(1) == unit_for(slot, 0));
    CHECK(params.at(2) == 0x00);
    CHECK(params.at(3) == 0x08);
    CHECK(params.at(4) == 0x00);
    CHECK(params.at(5) == 0x00);
  }
}

TEST_CASE(
    "Harddisk firmware: with no image the boot takes the trampoline into the "
    "fallback, reads the machine byte, sets the scan pointer to its own slot "
    "and re-enters SLOOP, and the scan ends in BASIC when nothing is below") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const uint16_t page = slot_page(slot);

  linapple_reset_hard();
  // The scan enters SLOOP before it looks at any slot, so the fallback is
  // the first mark; the scan pointer is read on the fallback's own re-entry.
  const std::vector<uint16_t> to_fail =
      run_recording(static_cast<uint16_t>(page + fail_offset), boot_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == page + fail_offset);
  CHECK(visited(to_fail, page));
  CHECK(visited_in_order(to_fail,
                         {static_cast<uint16_t>(page + driver_offset),
                          static_cast<uint16_t>(page + ret1_offset),
                          static_cast<uint16_t>(page + trampoline_offset)}));
  CHECK_FALSE(visited(to_fail, static_cast<uint16_t>(page + ret2_offset)));

  step_once();
  run_recording(monitor_sloop, boot_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == monitor_sloop);
  // LOC0/LOC1 as the scan left them before JMP ($0000) (Apple IIe Technical
  // Reference Manual, p. 307).
  CHECK(mem[0x01] == static_cast<uint8_t>(0xC0 | slot));
  CHECK(mem[0x00] == 0x00);

  step_once();
  const std::vector<uint16_t> to_basic =
      run_recording(basic_cold_start, boot_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == basic_cold_start);
  CHECK(visited(to_basic, monitor_no_slot));
  CHECK_FALSE(visited(to_basic, page));
}

#if defined(ENABLE_PERIPHERAL_DISK)
TEST_CASE(
    "Harddisk firmware: with no image and a Disk II below, the fallback scan "
    "reaches $C600 and that disk boots to the prompt") {
  const int slot = 7;
  TestConfig_t::Description_t description = machine_with_card(slot);
  description.slots[5] = "Disk II";
  description.extras.push_back({"Configuration", "Disk Turbo", "1"});
  description.extras.push_back(
      {"Slots", "Disk Image 1", Path::find_data_file("Master.dsk")});
  Machine_t machine(description);
  REQUIRE(peripheral_present(slot, harddisk_id));
  REQUIRE(peripheral_present(6, "linapple.disk_II"));

  linapple_reset_hard();
  const uint16_t fail = static_cast<uint16_t>(slot_page(slot) + fail_offset);
  run_recording(fail, boot_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == fail);

  step_once();
  const std::vector<uint16_t> to_disk = run_recording(0xC600, boot_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == 0xC600);
  CHECK(visited(to_disk, monitor_sloop));
  CHECK_FALSE(visited(to_disk, slot_page(slot)));

  CHECK(run_frames_until_prompt(boot_frame_cap));
}

#if defined(ENABLE_PERIPHERAL_KEYBOARD)
TEST_CASE(
    "Harddisk firmware: a card below a booting Disk II is entered by PR#n "
    "from the prompt and boots its block 0") {
  const int slot = 5;
  TestConfig_t::Description_t description = machine_with_card(slot);
  description.slots[5] = "Disk II";
  description.extras.push_back({"Configuration", "Disk Turbo", "1"});
  description.extras.push_back(
      {"Slots", "Disk Image 1", Path::find_data_file("Master.dsk")});
  Machine_t machine(description);
  REQUIRE(peripheral_present(slot, harddisk_id));
  REQUIRE(peripheral_present(0, "linapple.keyboard"));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  insert(slot, 0, image.path());
  const std::array<uint8_t, block_size> block0 =
      read_block_from_file(image.path(), 0);

  linapple_reset_hard();
  REQUIRE(run_frames_until_prompt(boot_frame_cap));

  // A key held a hundred machine frames repeats on a //e, so strokes are
  // counted in the machine's frames with the drive's turbo off.
  const bool disk_turbo = linapple_set_disk_turbo(false);
  for (const char c : std::string("PR#5\r")) {
    const uint8_t key = (c == '\r') ? static_cast<uint8_t>(linapple_key_return)
                                    : static_cast<uint8_t>(c);
    linapple_set_key_state(key, true);
    linapple_run_frame(frame_cycles);
    linapple_set_key_state(key, false);
    linapple_run_frame(frame_cycles);
  }
  linapple_set_disk_turbo(disk_turbo);
  for (int frame = 0; frame < 4; ++frame) {
    linapple_run_frame(frame_cycles);
  }

  // Block 0 jumps to itself, so a boot that reached $0801 is still there.
  CHECK(cpu_get_registers()->pc == boot_entry);
  CHECK(cpu_get_registers()->x == static_cast<uint8_t>(slot << 4));
  for (size_t i = 0; i < block_size; ++i) {
    CAPTURE(i);
    CHECK(mem[boot_buffer + i] == block0.at(i));
  }
}
#endif
#endif

TEST_CASE(
    "Harddisk firmware: on an Apple II, whose Monitor has no slot scan, the "
    "fallback goes to the Monitor's reset entry") {
  const int slot = 7;
  TestConfig_t::Description_t description = machine_with_card(slot);
  description.machine_type = TestConfig_t::machine_apple2;
  // The bridge takes the model from the controller, not from the file.
  const Apple2Type_t previous = linapple_get_apple2_type();
  linapple_set_apple2_type(A2TYPE_APPLE2);
  {
    Machine_t machine(description);
    REQUIRE(peripheral_present(slot, harddisk_id));
    // Apple IIe Technical Reference Manual, p. 136: $38 names the II.
    REQUIRE(mem[0xFBB3] == 0x38);

    TestFixtures::enter_at({slot_page(slot), 0, 0, 0});
    const std::vector<uint16_t> trace =
        run_recording(monitor_reset_entry, boot_cycle_cap);
    CHECK(cpu_get_registers()->pc == monitor_reset_entry);
    CHECK(visited(trace, static_cast<uint16_t>(slot_page(slot) + fail_offset)));
    CHECK_FALSE(visited(trace, monitor_sloop));
  }
  linapple_set_apple2_type(previous);
}

TEST_CASE(
    "Harddisk firmware: STATUS returns the block count in X and Y with A zero "
    "and the carry clear, never checks the block, and answers NO DEVICE "
    "CONNECTED for an empty drive") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const uint16_t count_low = io_base(slot) + 5;
  const uint16_t count_high = io_base(slot) + 6;

  CHECK(peek_io(count_low) == 0x00);
  CHECK(peek_io(count_high) == 0x00);

  const auto hdv = TestFixtures::create_ephemeral("minimal-block.hdv");
  insert(slot, 0, hdv.path());

  CallResult_t result =
      call_card(slot, {prodos_status, unit_for(slot, 0), 0, 0});
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  CHECK(result.x == (hdv_blocks & 0xFF));
  CHECK(result.y == (hdv_blocks >> 8));
  CHECK(peek_io(count_low) == 0x10);
  CHECK(peek_io(count_high) == 0x00);

  // The boot's STATUS carries $Cn in the block-high register.
  result = call_card(slot, {prodos_status, unit_for(slot, 0), 0, 0xFFFF});
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  CHECK(result.x == (hdv_blocks & 0xFF));
  CHECK(result.y == (hdv_blocks >> 8));

  const auto po = TestFixtures::create_ephemeral("minimal.po");
  insert(slot, 0, po.path());
  result = call_card(slot, {prodos_status, unit_for(slot, 0), 0, 0});
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  CHECK(result.x == (po_blocks & 0xFF));
  CHECK(result.y == (po_blocks >> 8));

  result = call_card(slot, {prodos_status, unit_for(slot, 1), 0, 0});
  CHECK(result.carry());
  CHECK(result.a == prodos_no_device);
}

TEST_CASE(
    "Harddisk firmware: READ fills the caller's buffer with the block, leaves "
    "the parameter block as it found it, wraps the data port after 512 "
    "bytes, and refuses a block past the end or an empty drive") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto hdv = TestFixtures::create_ephemeral("minimal-block.hdv");
  insert(slot, 0, hdv.path());
  const uint16_t data_port = io_base(slot) + 4;

  for (uint16_t block = 1; block < hdv_blocks; ++block) {
    CAPTURE(block);
    fill(read_buffer, block_size, 0xEE);
    const CallResult_t result =
        call_card(slot, {prodos_read, unit_for(slot, 0), read_buffer, block});
    CHECK_FALSE(result.carry());
    CHECK(result.a == prodos_ok);
    CHECK(buffer_all(read_buffer, static_cast<uint8_t>(block)));
    const std::array<uint8_t, 6> params = parameter_block();
    CHECK(params.at(2) == (read_buffer & 0xFF));
    CHECK(params.at(3) == (read_buffer >> 8));
    CHECK(params.at(4) == (block & 0xFF));
    CHECK(params.at(5) == (block >> 8));
  }

  // Block 0 is the one block whose bytes differ, so the wrap shows.
  const std::array<uint8_t, block_size> block0 =
      read_block_from_file(hdv.path(), 0);
  CallResult_t result =
      call_card(slot, {prodos_read, unit_for(slot, 0), read_buffer, 0});
  REQUIRE_FALSE(result.carry());
  for (size_t i = 0; i < block_size; ++i) {
    CHECK(peek_io(data_port) == block0.at(i));
  }
  CHECK(peek_io(data_port) == block0.at(0));
  CHECK(peek_io(data_port) == block0.at(1));

  fill(read_buffer, block_size, 0xEE);
  result = call_card(slot,
                     {prodos_read, unit_for(slot, 0), read_buffer, hdv_blocks});
  CHECK(result.carry());
  CHECK(result.a == prodos_io_error);
  CHECK(buffer_all(read_buffer, 0xEE));

  result = call_card(slot, {prodos_read, unit_for(slot, 1), read_buffer, 1});
  CHECK(result.carry());
  CHECK(result.a == prodos_no_device);
}

TEST_CASE(
    "Harddisk firmware: WRITE pushes the caller's block through the data "
    "port and reads back, both pages in order, and a block past the end is "
    "an I/O error") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto hdv = TestFixtures::create_ephemeral("minimal-block.hdv");
  insert(slot, 0, hdv.path());

  std::array<uint8_t, block_size> pattern{};
  for (size_t i = 0; i < block_size; ++i) {
    pattern.at(i) = static_cast<uint8_t>((i & 0xFF) ^ (i >> 8));
  }
  TestFixtures::ScopedCore_t::poke(write_buffer, pattern);

  CallResult_t result =
      call_card(slot, {prodos_write, unit_for(slot, 0), write_buffer, 5});
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  const std::array<uint8_t, 6> params = parameter_block();
  CHECK(params.at(2) == (write_buffer & 0xFF));
  CHECK(params.at(3) == (write_buffer >> 8));

  fill(read_buffer, block_size, 0xEE);
  result = call_card(slot, {prodos_read, unit_for(slot, 0), read_buffer, 5});
  REQUIRE_FALSE(result.carry());
  for (size_t i = 0; i < block_size; ++i) {
    CAPTURE(i);
    CHECK(mem[read_buffer + i] == pattern.at(i));
  }
  result = call_card(slot, {prodos_read, unit_for(slot, 0), read_buffer, 4});
  REQUIRE_FALSE(result.carry());
  CHECK(buffer_all(read_buffer, 4));
  result = call_card(slot, {prodos_read, unit_for(slot, 0), read_buffer, 6});
  REQUIRE_FALSE(result.carry());
  CHECK(buffer_all(read_buffer, 6));

  result = call_card(
      slot, {prodos_write, unit_for(slot, 0), write_buffer, hdv_blocks});
  CHECK(result.carry());
  CHECK(result.a == prodos_io_error);
}

TEST_CASE(
    "Harddisk firmware: a drive the user protected answers WRITE and FORMAT "
    "with WRITE PROTECTED, still reads, and says so in the status") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto hdv = TestFixtures::create_ephemeral("minimal-block.hdv");
  insert(slot, 0, hdv.path());
  set_protect(slot, 0, true);
  CHECK(status(slot).drive0_write_protected == 1);

  CallResult_t result =
      call_card(slot, {prodos_write, unit_for(slot, 0), write_buffer, 3});
  CHECK(result.carry());
  CHECK(result.a == prodos_write_protected);

  result = call_card(slot, {prodos_format, unit_for(slot, 0), 0, 0});
  CHECK(result.carry());
  CHECK(result.a == prodos_write_protected);

  result = call_card(slot, {prodos_read, unit_for(slot, 0), read_buffer, 3});
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  CHECK(buffer_all(read_buffer, 3));
}

TEST_CASE(
    "Harddisk firmware: FORMAT succeeds on a writable image and changes "
    "nothing in it; an empty drive answers NO DEVICE CONNECTED") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto hdv = TestFixtures::create_ephemeral("minimal-block.hdv");
  const std::vector<uint8_t> before = read_whole_file(hdv.path());
  insert(slot, 0, hdv.path());

  CallResult_t result =
      call_card(slot, {prodos_format, unit_for(slot, 0), 0, 0});
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  eject(slot, 0);
  CHECK(read_whole_file(hdv.path()) == before);

  result = call_card(slot, {prodos_format, unit_for(slot, 1), 0, 0});
  CHECK(result.carry());
  CHECK(result.a == prodos_no_device);
}

TEST_CASE(
    "Harddisk firmware: bit 7 of the unit selects the drive at execute time "
    "through one register set, and the boot takes drive 1 only") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto hdv = TestFixtures::create_ephemeral("minimal-block.hdv");
  const auto po = TestFixtures::create_ephemeral("minimal.po");
  insert(slot, 0, hdv.path());
  insert(slot, 1, po.path());

  CallResult_t result =
      call_card(slot, {prodos_read, unit_for(slot, 0), read_buffer, 1});
  REQUIRE_FALSE(result.carry());
  CHECK(buffer_all(read_buffer, 0x01));

  result = call_card(slot, {prodos_read, unit_for(slot, 1), read_buffer, 2});
  REQUIRE_FALSE(result.carry());
  // The volume directory header: storage type and name length $F7, then the
  // name (ProDOS 8 Technical Reference Manual, B.2.2).
  CHECK(mem[read_buffer + 4] == 0xF7);
  CHECK(std::memcmp(&mem[read_buffer + 5], "MINIMAL", 7) == 0);

  // The block register is the controller's, whichever unit is latched.
  const uint16_t unit_reg = io_base(slot) + 1;
  const uint16_t block_low = io_base(slot) + 2;
  poke_io(unit_reg, unit_for(slot, 0));
  poke_io(block_low, 0x05);
  poke_io(unit_reg, unit_for(slot, 1));
  CHECK(peek_io(block_low) == 0x05);

  // Drive 2 alone does not boot: the firmware asks drive 1 and falls back.
  eject(slot, 0);
  linapple_reset_hard();
  const uint16_t fail = static_cast<uint16_t>(slot_page(slot) + fail_offset);
  const std::vector<uint16_t> trace = run_recording(fail, boot_cycle_cap);
  CHECK(cpu_get_registers()->pc == fail);
  CHECK_FALSE(visited(trace, boot_entry));
}

TEST_CASE(
    "Harddisk firmware: the driver entry a resident ProDOS remembers, $C746, "
    "still answers READ and STATUS for a card in slot 7") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto hdv = TestFixtures::create_ephemeral("minimal-block.hdv");
  insert(slot, 0, hdv.path());

  fill(read_buffer, block_size, 0xEE);
  CallResult_t result =
      call_driver(0xC746, {prodos_read, unit_for(slot, 0), read_buffer, 1});
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  CHECK(buffer_all(read_buffer, 0x01));

  result = call_driver(0xC746, {prodos_status, unit_for(slot, 0), 0, 0});
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  CHECK(result.x == (hdv_blocks & 0xFF));
  CHECK(result.y == (hdv_blocks >> 8));
}
