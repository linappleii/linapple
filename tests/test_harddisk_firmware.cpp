// SPDX-License-Identifier: GPL-2.0-only
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"
#if ENABLE_DEBUGGER
#include "Debugger/Debugger_Assembler.h"
#include "Debugger/Debugger_Types.h"
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

// The unit byte a stepped call carries must name the slot the card is in, or
// the firmware addresses another slot's page and reads the floating bus; the
// manager is asked rather than the description trusted.
auto require_card_in(int slot) -> void {
  REQUIRE(peripheral_present(slot, harddisk_id));
  REQUIRE(peripheral_slot_of(harddisk_id) == slot);
}

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
  FilePtr file{fopen(path.c_str(), "rb"), fclose};
  REQUIRE(file != nullptr);
  REQUIRE(fseek(file.get(), static_cast<long>(block * block_size), SEEK_SET) ==
          0);
  REQUIRE(fread(bytes.data(), 1, bytes.size(), file.get()) == bytes.size());
  return bytes;
}

auto read_whole_file(const std::string& path) -> std::vector<uint8_t> {
  FilePtr file{fopen(path.c_str(), "rb"), fclose};
  REQUIRE(file != nullptr);
  const int64_t size = Path::file_size(file.get());
  REQUIRE(size >= 0);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  REQUIRE(fread(bytes.data(), 1, bytes.size(), file.get()) == bytes.size());
  return bytes;
}

// Every instruction the 6502 fetches, until the sentinel or the cap: which
// addresses a boot passed through says as much as where it ended.
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
      0x0B,
  };
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
      static_cast<uint8_t>((program_start + 41) >> 8),
  };
  const uint16_t sentinel = program_start + 41;
  TestFixtures::ScopedCore_t::poke(program_start, program);
  TestFixtures::enter_at({program_start, 0, 0, 0});
  TestFixtures::step_until_pc(sentinel, call_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == sentinel);
  return CallResult_t{
      mem[results_at],
      mem[results_at + 1],
      mem[results_at + 2],
      mem[results_at + 3],
  };
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

auto buffer_of(uint8_t value) -> std::array<uint8_t, block_size> {
  std::array<uint8_t, block_size> bytes{};
  bytes.fill(value);
  return bytes;
}

class ScopedFileMode_t {
 public:
  ScopedFileMode_t(std::string path, mode_t new_mode, mode_t restore_mode)
      : path_(std::move(path)), restore_mode_(restore_mode) {
    chmod(path_.c_str(), new_mode);
  }
  ~ScopedFileMode_t() { chmod(path_.c_str(), restore_mode_); }
  ScopedFileMode_t(const ScopedFileMode_t&) = delete;
  auto operator=(const ScopedFileMode_t&) -> ScopedFileMode_t& = delete;
  ScopedFileMode_t(ScopedFileMode_t&&) = delete;
  auto operator=(ScopedFileMode_t&&) -> ScopedFileMode_t& = delete;

 private:
  std::string path_;
  mode_t restore_mode_;
};

// Whatever protects the drive, the driver answers the two writing commands
// with the one ProDOS code, reads as before, and the status agrees.
auto check_protected(int slot, int drive) -> void {
  const HarddiskStatus_t current = status(slot);
  CHECK((drive == 0 ? current.drive0_loaded : current.drive1_loaded) == 1);
  CHECK((drive == 0 ? current.drive0_write_protected
                    : current.drive1_write_protected) == 1);

  CallResult_t result =
      call_card(slot, {prodos_write, unit_for(slot, drive), write_buffer, 3});
  CHECK(result.carry());
  CHECK(result.a == prodos_write_protected);

  result = call_card(slot, {prodos_format, unit_for(slot, drive), 0, 0});
  CHECK(result.carry());
  CHECK(result.a == prodos_write_protected);

  fill(read_buffer, block_size, 0xEE);
  result =
      call_card(slot, {prodos_read, unit_for(slot, drive), read_buffer, 3});
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  CHECK(buffer_all(read_buffer, 3));
}

// The boots that end at a DOS prompt need the Disk II, as their cases do.
#ifdef ENABLE_PERIPHERAL_DISK
auto text_row_begins_with(char first) -> bool {
  static const std::array<uint16_t, 24> row_offsets = {
      0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
      0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0,
  };
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

enum Mnemonic : uint8_t {
  op_lda,
  op_sta,
  op_ldx,
  op_ldy,
  op_tax,
  op_pha,
  op_iny,
  op_inc,
  op_dec,
  op_and,
  op_ora,
  op_cmp,
  op_asl,
  op_lsr,
  op_beq,
  op_bne,
  op_bcs,
  op_jmp,
  op_clc,
  op_sec,
  op_rts,
};

enum Mode : uint8_t {
  mode_implied,
  mode_accumulator,
  mode_immediate,
  mode_zero_page,
  mode_absolute,
  mode_absolute_x,
  mode_indirect_y,
  mode_relative,
};

struct Opcode_t {
  Mnemonic mnemonic;
  Mode mode;
  uint8_t opcode;
};

// The 28 (mnemonic, mode) pairs the listing uses, from the 6502 opcode map.
constexpr std::array<Opcode_t, 28> opcodes = {
    {
        {op_lda, mode_immediate, 0xA9},   {op_lda, mode_zero_page, 0xA5},
        {op_lda, mode_absolute, 0xAD},    {op_lda, mode_absolute_x, 0xBD},
        {op_lda, mode_indirect_y, 0xB1},  {op_sta, mode_zero_page, 0x85},
        {op_sta, mode_absolute_x, 0x9D},  {op_sta, mode_indirect_y, 0x91},
        {op_ldx, mode_zero_page, 0xA6},   {op_ldy, mode_immediate, 0xA0},
        {op_ldy, mode_absolute_x, 0xBC},  {op_tax, mode_implied, 0xAA},
        {op_pha, mode_implied, 0x48},     {op_iny, mode_implied, 0xC8},
        {op_inc, mode_zero_page, 0xE6},   {op_dec, mode_zero_page, 0xC6},
        {op_and, mode_immediate, 0x29},   {op_ora, mode_immediate, 0x09},
        {op_cmp, mode_immediate, 0xC9},   {op_asl, mode_accumulator, 0x0A},
        {op_lsr, mode_accumulator, 0x4A}, {op_beq, mode_relative, 0xF0},
        {op_bne, mode_relative, 0xD0},    {op_bcs, mode_relative, 0xB0},
        {op_jmp, mode_absolute, 0x4C},    {op_clc, mode_implied, 0x18},
        {op_sec, mode_implied, 0x38},     {op_rts, mode_implied, 0x60},
    },
};

auto mnemonic_name(Mnemonic mnemonic) -> const char* {
  switch (mnemonic) {
    case op_lda:
      return "LDA";
    case op_sta:
      return "STA";
    case op_ldx:
      return "LDX";
    case op_ldy:
      return "LDY";
    case op_tax:
      return "TAX";
    case op_pha:
      return "PHA";
    case op_iny:
      return "INY";
    case op_inc:
      return "INC";
    case op_dec:
      return "DEC";
    case op_and:
      return "AND";
    case op_ora:
      return "ORA";
    case op_cmp:
      return "CMP";
    case op_asl:
      return "ASL";
    case op_lsr:
      return "LSR";
    case op_beq:
      return "BEQ";
    case op_bne:
      return "BNE";
    case op_bcs:
      return "BCS";
    case op_jmp:
      return "JMP";
    case op_clc:
      return "CLC";
    case op_sec:
      return "SEC";
    case op_rts:
      return "RTS";
  }
  return "???";
}

auto instruction_length(Mode mode) -> uint8_t {
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
  Mnemonic mnemonic;
  Mode mode;
  // The immediate or zero-page byte, the absolute address, or for a branch
  // the target's offset in the page.
  uint16_t operand;
};

// Labels: RET1 $1E, RET2 $38, FAILJ $3F, DRIVER $46, W1 $62, W2 $6C, EXEC
// $78, R1 $8A, R2 $94, DONE $9E, STATUS $A2, ERROR $AD, FAIL $AF, MONITOR $C7.
const Row_t listing[] = {
    {0x00, op_lda, mode_immediate, 0x20},
    {0x02, op_lda, mode_immediate, 0x00},
    {0x04, op_lda, mode_immediate, 0x03},
    {0x06, op_lda, mode_immediate, 0x3C},
    {0x08, op_lda, mode_immediate, 0xC0},
    {0x0A, op_sta, mode_zero_page, 0x47},
    {0x0C, op_asl, mode_accumulator, 0},
    {0x0D, op_asl, mode_accumulator, 0},
    {0x0E, op_asl, mode_accumulator, 0},
    {0x0F, op_asl, mode_accumulator, 0},
    {0x10, op_sta, mode_zero_page, 0x43},
    {0x12, op_lda, mode_zero_page, 0x47},
    {0x14, op_pha, mode_implied, 0},
    {0x15, op_lda, mode_immediate, 0x1D},
    {0x17, op_pha, mode_implied, 0},
    {0x18, op_lda, mode_immediate, 0x00},
    {0x1A, op_sta, mode_zero_page, 0x42},
    {0x1C, op_beq, mode_relative, 0x46},
    {0x1E, op_bcs, mode_relative, 0x3F},
    {0x20, op_lda, mode_zero_page, 0x47},
    {0x22, op_pha, mode_implied, 0},
    {0x23, op_lda, mode_immediate, 0x37},
    {0x25, op_pha, mode_implied, 0},
    {0x26, op_lda, mode_immediate, 0x00},
    {0x28, op_sta, mode_zero_page, 0x44},
    {0x2A, op_sta, mode_zero_page, 0x46},
    {0x2C, op_sta, mode_zero_page, 0x47},
    {0x2E, op_lda, mode_immediate, 0x08},
    {0x30, op_sta, mode_zero_page, 0x45},
    {0x32, op_lda, mode_immediate, 0x01},
    {0x34, op_sta, mode_zero_page, 0x42},
    {0x36, op_bne, mode_relative, 0x46},
    {0x38, op_bcs, mode_relative, 0x3F},
    {0x3A, op_ldx, mode_zero_page, 0x43},
    {0x3C, op_jmp, mode_absolute, 0x0801},
    {0x3F, op_bcs, mode_relative, 0xAF},
    {0x46, op_lda, mode_zero_page, 0x43},
    {0x48, op_and, mode_immediate, 0x70},
    {0x4A, op_tax, mode_implied, 0},
    {0x4B, op_lda, mode_zero_page, 0x43},
    {0x4D, op_sta, mode_absolute_x, 0xC081},
    {0x50, op_lda, mode_zero_page, 0x46},
    {0x52, op_sta, mode_absolute_x, 0xC082},
    {0x55, op_lda, mode_zero_page, 0x47},
    {0x57, op_sta, mode_absolute_x, 0xC083},
    {0x5A, op_lda, mode_zero_page, 0x42},
    {0x5C, op_cmp, mode_immediate, 0x02},
    {0x5E, op_bne, mode_relative, 0x78},
    {0x60, op_ldy, mode_immediate, 0x00},
    {0x62, op_lda, mode_indirect_y, 0x44},
    {0x64, op_sta, mode_absolute_x, 0xC084},
    {0x67, op_iny, mode_implied, 0},
    {0x68, op_bne, mode_relative, 0x62},
    {0x6A, op_inc, mode_zero_page, 0x45},
    {0x6C, op_lda, mode_indirect_y, 0x44},
    {0x6E, op_sta, mode_absolute_x, 0xC084},
    {0x71, op_iny, mode_implied, 0},
    {0x72, op_bne, mode_relative, 0x6C},
    {0x74, op_dec, mode_zero_page, 0x45},
    {0x76, op_lda, mode_zero_page, 0x42},
    {0x78, op_sta, mode_absolute_x, 0xC080},
    {0x7B, op_lda, mode_absolute_x, 0xC080},
    {0x7E, op_bne, mode_relative, 0xAD},
    {0x80, op_lda, mode_zero_page, 0x42},
    {0x82, op_beq, mode_relative, 0xA2},
    {0x84, op_cmp, mode_immediate, 0x01},
    {0x86, op_bne, mode_relative, 0x9E},
    {0x88, op_ldy, mode_immediate, 0x00},
    {0x8A, op_lda, mode_absolute_x, 0xC084},
    {0x8D, op_sta, mode_indirect_y, 0x44},
    {0x8F, op_iny, mode_implied, 0},
    {0x90, op_bne, mode_relative, 0x8A},
    {0x92, op_inc, mode_zero_page, 0x45},
    {0x94, op_lda, mode_absolute_x, 0xC084},
    {0x97, op_sta, mode_indirect_y, 0x44},
    {0x99, op_iny, mode_implied, 0},
    {0x9A, op_bne, mode_relative, 0x94},
    {0x9C, op_dec, mode_zero_page, 0x45},
    {0x9E, op_lda, mode_immediate, 0x00},
    {0xA0, op_clc, mode_implied, 0},
    {0xA1, op_rts, mode_implied, 0},
    {0xA2, op_ldy, mode_absolute_x, 0xC086},
    {0xA5, op_lda, mode_absolute_x, 0xC085},
    {0xA8, op_tax, mode_implied, 0},
    {0xA9, op_lda, mode_immediate, 0x00},
    {0xAB, op_clc, mode_implied, 0},
    {0xAC, op_rts, mode_implied, 0},
    {0xAD, op_sec, mode_implied, 0},
    {0xAE, op_rts, mode_implied, 0},
    {0xAF, op_lda, mode_absolute, 0xFBB3},
    {0xB2, op_cmp, mode_immediate, 0x38},
    {0xB4, op_beq, mode_relative, 0xC7},
    {0xB6, op_lda, mode_zero_page, 0x43},
    {0xB8, op_lsr, mode_accumulator, 0},
    {0xB9, op_lsr, mode_accumulator, 0},
    {0xBA, op_lsr, mode_accumulator, 0},
    {0xBB, op_lsr, mode_accumulator, 0},
    {0xBC, op_ora, mode_immediate, 0xC0},
    {0xBE, op_sta, mode_zero_page, 0x01},
    {0xC0, op_lda, mode_immediate, 0x00},
    {0xC2, op_sta, mode_zero_page, 0x00},
    {0xC4, op_jmp, mode_absolute, 0xFABA},
    {0xC7, op_jmp, mode_absolute, 0xFF59},
};

// The five unused bytes after the trampoline and the run from $CA to $FB.
constexpr uint8_t unused_after_trampoline = 0x41;
constexpr uint8_t code_end = 0xCA;
constexpr uint8_t tail_blocks_low = 0xFC;
constexpr uint8_t tail_status = 0xFE;
constexpr uint8_t tail_entry = 0xFF;
constexpr uint8_t status_byte = 0xDF;

auto opcode_for(Mnemonic mnemonic, Mode mode) -> uint8_t {
  for (const Opcode_t& entry : opcodes) {
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
  for (const Row_t& row : listing) {
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
  require_card_in(slot);

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
  for (const Row_t& row : listing) {
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
    require_card_in(slot);
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
    require_card_in(slot);
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
  for (const int slot : {7, 5}) {
    CAPTURE(slot);
    Machine_t machine(machine_with_card(slot));
    require_card_in(slot);
    const uint16_t page = slot_page(slot);

    linapple_reset_hard();
    // The scan enters SLOOP before it looks at any slot, so the fallback is
    // the first mark; the scan pointer is read on the fallback's own re-entry.
    const std::vector<uint16_t> to_fail = run_recording(
        static_cast<uint16_t>(page + fail_offset), boot_cycle_cap);
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
}

#ifdef ENABLE_PERIPHERAL_DISK
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
  require_card_in(slot);
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

#ifdef ENABLE_PERIPHERAL_KEYBOARD
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
  require_card_in(slot);
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
  const Apple2Type previous = linapple_get_apple2_type();
  linapple_set_apple2_type(A2TYPE_APPLE2);
  {
    Machine_t machine(description);
    require_card_in(slot);
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
  require_card_in(slot);
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
  require_card_in(slot);
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
    "port, the file holds it before the card is touched again and after the "
    "eject, both pages land in order, and a block past the end is an I/O "
    "error") {
  const int slot = 7;
  const auto hdv = TestFixtures::create_ephemeral("minimal-block.hdv");
  std::array<uint8_t, block_size> pattern{};
  for (size_t i = 0; i < block_size; ++i) {
    pattern.at(i) = static_cast<uint8_t>((i & 0xFF) ^ (i >> 8));
  }
  {
    Machine_t machine(machine_with_card(slot));
    require_card_in(slot);
    insert(slot, 0, hdv.path());
    TestFixtures::ScopedCore_t::poke(write_buffer, pattern);

    CallResult_t result =
        call_card(slot, {prodos_write, unit_for(slot, 0), write_buffer, 5});
    CHECK_FALSE(result.carry());
    CHECK(result.a == prodos_ok);
    const std::array<uint8_t, 6> params = parameter_block();
    CHECK(params.at(2) == (write_buffer & 0xFF));
    CHECK(params.at(3) == (write_buffer >> 8));

    // The block is in the file as soon as the call returns, seen through a
    // handle of the test's own before the card touches the file again.
    CHECK(read_block_from_file(hdv.path(), 5) == pattern);
    CHECK(read_block_from_file(hdv.path(), 4) == buffer_of(4));
    CHECK(read_block_from_file(hdv.path(), 6) == buffer_of(6));

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

    eject(slot, 0);
    CHECK(read_block_from_file(hdv.path(), 5) == pattern);
  }

  // Another machine mounting the file finds the block there.
  Machine_t second(machine_with_card(slot));
  require_card_in(slot);
  insert(slot, 0, hdv.path());
  fill(read_buffer, block_size, 0xEE);
  const CallResult_t again =
      call_card(slot, {prodos_read, unit_for(slot, 0), read_buffer, 5});
  REQUIRE_FALSE(again.carry());
  for (size_t i = 0; i < block_size; ++i) {
    CAPTURE(i);
    CHECK(mem[read_buffer + i] == pattern.at(i));
  }
}

TEST_CASE(
    "Harddisk firmware: a drive protected by the user, by the file's mode, by "
    "a locked 2MG or by an archive answers WRITE and FORMAT with WRITE "
    "PROTECTED, still reads, and says so in the status") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  require_card_in(slot);

  const auto user = TestFixtures::create_ephemeral("minimal-block.hdv");
  insert(slot, 0, user.path());
  set_protect(slot, 0, true);
  check_protected(slot, 0);

  // Superuser bypasses the file mode, as test_disk_prot does.
  if (getuid() != 0) {
    const auto readonly = TestFixtures::create_ephemeral("minimal-block.hdv");
    const ScopedFileMode_t mode(readonly.path(), 0444, 0644);
    insert(slot, 0, readonly.path());
    check_protected(slot, 0);
    eject(slot, 0);
  }

  const auto locked =
      TestFixtures::create_ephemeral("minimal-block-locked.2mg");
  insert(slot, 0, locked.path());
  check_protected(slot, 0);

  const auto archive = TestFixtures::create_ephemeral("minimal-block.hdv.gz");
  insert(slot, 0, archive.path());
  check_protected(slot, 0);
}

TEST_CASE(
    "Harddisk firmware: FORMAT succeeds on a writable image and changes "
    "nothing in it; an empty drive answers NO DEVICE CONNECTED") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  require_card_in(slot);
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
  require_card_in(slot);
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
  require_card_in(slot);
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

namespace {

// A driver that serves any block number at all, so the one bound a READ or
// WRITE meets is the controller's own.
constexpr uint32_t boundless_blocks = 4;

auto boundless_probe(const uint8_t* /*unused*/, size_t /*unused*/,
                     uint64_t /*unused*/, const char* ext_hint)
    -> HarddiskProbe {
  return (ext_hint != nullptr && std::strcmp(ext_hint, ".boundless") == 0)
             ? harddisk_probe_definite
             : harddisk_probe_no;
}

auto boundless_open(const char* /*unused*/, uint32_t /*unused*/,
                    bool /*unused*/, void** out_instance) -> HarddiskError {
  static int instance = 0;
  *out_instance = &instance;
  return harddisk_err_none;
}

auto boundless_close(void* /*unused*/) -> void {}

auto boundless_is_write_protected(void* /*unused*/) -> bool { return false; }

auto boundless_read_block(void* /*unused*/, uint32_t block, uint8_t* buffer)
    -> HarddiskError {
  std::memset(buffer, static_cast<int>(block & 0xFF), block_size);
  return harddisk_err_none;
}

auto boundless_write_block(void* /*unused*/, uint32_t /*unused*/,
                           const uint8_t* /*unused*/) -> HarddiskError {
  return harddisk_err_none;
}

auto boundless_get_total_blocks(void* /*unused*/) -> uint32_t {
  return boundless_blocks;
}

const char* const boundless_exts[] = {"boundless", nullptr};

const HarddiskFormatDriver_t g_boundless_driver = {
    .abi_version = harddisk_format_abi_version,
    .capabilities = harddisk_driver_cap_write,
    .name = "Boundless",
    .supported_exts = boundless_exts,
    .probe = boundless_probe,
    .open = boundless_open,
    .close = boundless_close,
    .is_write_protected = boundless_is_write_protected,
    .read_block = boundless_read_block,
    .write_block = boundless_write_block,
    .get_total_blocks = boundless_get_total_blocks,
};

// Forgets the driver above whatever happens, so the registry the next case
// sees is the built-in one.
struct ScopedBoundlessDriver_t {
  ScopedBoundlessDriver_t() { harddisk_loader_register(&g_boundless_driver); }
  ~ScopedBoundlessDriver_t() { harddisk_loader_reset(); }
  ScopedBoundlessDriver_t(const ScopedBoundlessDriver_t&) = delete;
  auto operator=(const ScopedBoundlessDriver_t&)
      -> ScopedBoundlessDriver_t& = delete;
  ScopedBoundlessDriver_t(ScopedBoundlessDriver_t&&) = delete;
  auto operator=(ScopedBoundlessDriver_t&&)
      -> ScopedBoundlessDriver_t& = delete;
};

}  // namespace

TEST_CASE(
    "Harddisk firmware: the controller itself refuses the block one past the "
    "volume's last with an I/O error, READ and WRITE alike, even when the "
    "medium would serve it") {
  const int slot = 7;
  Machine_t machine(machine_with_card(slot));
  require_card_in(slot);
  const ScopedBoundlessDriver_t driver;
  TestFixtures::ScopedTempDir_t dir("linapple_hdd_bound_");
  const std::string path = dir.path() + "/volume.boundless";
  {
    FilePtr file{fopen(path.c_str(), "wb"), fclose};
    REQUIRE(file != nullptr);
  }
  insert(slot, 0, path);
  REQUIRE(status(slot).drive0_loaded == 1);

  CallResult_t result =
      call_card(slot, {prodos_status, unit_for(slot, 0), 0, 0});
  REQUIRE_FALSE(result.carry());
  REQUIRE(result.x == boundless_blocks);
  REQUIRE(result.y == 0);

  // The last block is served, the one past it is beyond the volume ProDOS
  // was told about: $27 (ProDOS 8 Technical Reference Manual, 6.3.2).
  fill(read_buffer, block_size, 0xEE);
  result = call_card(slot, {
                               prodos_read,
                               unit_for(slot, 0),
                               read_buffer,
                               boundless_blocks - 1,
                           });
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  CHECK(buffer_all(read_buffer, boundless_blocks - 1));

  fill(read_buffer, block_size, 0xEE);
  result = call_card(
      slot, {prodos_read, unit_for(slot, 0), read_buffer, boundless_blocks});
  CHECK(result.carry());
  CHECK(result.a == prodos_io_error);
  CHECK(buffer_all(read_buffer, 0xEE));

  result = call_card(slot, {
                               prodos_write,
                               unit_for(slot, 0),
                               write_buffer,
                               boundless_blocks - 1,
                           });
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);

  result = call_card(
      slot, {prodos_write, unit_for(slot, 0), write_buffer, boundless_blocks});
  CHECK(result.carry());
  CHECK(result.a == prodos_io_error);
}
