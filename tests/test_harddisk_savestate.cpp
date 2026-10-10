// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/SaveStateManager.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig = TestFixtures::ScopedTestConfig;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr size_t block_size = 512;
constexpr size_t frame_size = 20;
constexpr uint16_t program_start = 0x0300;
constexpr uint16_t results_at = 0x1000;
constexpr uint16_t write_buffer = 0x2000;
constexpr uint16_t boot_entry = 0x0801;
constexpr uint8_t driver_offset = 0x46;
constexpr uint32_t call_cycle_cap = 20000;
constexpr uint32_t boot_cycle_cap = 2000000;
constexpr uint32_t ramp_block = 3;
constexpr uint16_t taken = 100;
constexpr uint16_t hdv_blocks = 16;

// ProDOS 8 Technical Reference Manual, 6.3.2.
constexpr uint8_t prodos_read = 0x01;
constexpr uint8_t prodos_write = 0x02;
constexpr uint8_t prodos_ok = 0x00;
constexpr uint8_t prodos_io_error = 0x27;

using Frame = std::array<uint8_t, frame_size>;

auto machine_with_card(int slot) -> TestConfig::Description {
  TestConfig::Description description;
  description.slots.at(static_cast<size_t>(slot - 1)) = "Harddisk";
  return description;
}

struct Machine {
  TestConfig config;
  TestFixtures::ScopedCore core;

  explicit Machine(const TestConfig::Description& description)
      : config(description), core(config) {
    peripheral_manager_init();
    linapple_register_peripherals();
    linapple_reset_hard();
  }
};

auto io_base(int slot) -> uint16_t {
  return static_cast<uint16_t>(0xC080 + (slot << 4));
}

auto unit_for(int slot, int drive) -> uint8_t {
  return static_cast<uint8_t>((slot << 4) | (drive != 0 ? 0x80 : 0));
}

auto peek(uint16_t addr) -> uint8_t {
  return io_map_dispatch(0, addr, 0, 0, 0);
}

auto poke(uint16_t addr, uint8_t value) -> void {
  io_map_dispatch(0, addr, 1, value, 0);
}

auto settle() -> void { peripheral_manager_think(0); }

auto insert(int slot, int drive, const std::string& path) -> void {
  HarddiskInsertCmd cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  std::strncpy(cmd.path, path.c_str(), sizeof(cmd.path) - 1);
  REQUIRE(peripheral_command(slot, harddisk_cmd_insert, &cmd, sizeof(cmd)) ==
          peripheral_ok);
  settle();
}

auto status(int slot) -> HarddiskStatus {
  HarddiskStatus out{};
  size_t size = sizeof(out);
  REQUIRE(peripheral_query(slot, harddisk_query_status, &out, &size) ==
          peripheral_ok);
  return out;
}

auto saved_frame(int slot) -> Frame {
  Frame frame{};
  size_t size = frame.size();
  peripheral_save_state(slot, frame.data(), &size);
  REQUIRE(size == frame.size());
  return frame;
}

auto ramp() -> std::array<uint8_t, block_size> {
  std::array<uint8_t, block_size> bytes{};
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes.at(i) = static_cast<uint8_t>(i & 0xFF);
  }
  return bytes;
}

struct CallResult {
  uint8_t a;
  uint8_t p;
  auto carry() const -> bool { return (p & 0x01) != 0; }
};

// The MLI's parameter block, a JSR to the driver and A and P stored.
auto call_driver(int slot, uint8_t command, uint8_t unit, uint16_t buffer,
                 uint16_t block) -> CallResult {
  const uint16_t entry =
      static_cast<uint16_t>(0xC000 + (slot << 8) + driver_offset);
  const std::array<uint8_t, 38> program = {
      0xA9,
      command,
      0x85,
      0x42,
      0xA9,
      unit,
      0x85,
      0x43,
      0xA9,
      static_cast<uint8_t>(buffer & 0xFF),
      0x85,
      0x44,
      0xA9,
      static_cast<uint8_t>(buffer >> 8),
      0x85,
      0x45,
      0xA9,
      static_cast<uint8_t>(block & 0xFF),
      0x85,
      0x46,
      0xA9,
      static_cast<uint8_t>(block >> 8),
      0x85,
      0x47,
      0x20,
      static_cast<uint8_t>(entry & 0xFF),
      static_cast<uint8_t>(entry >> 8),
      0x8D,
      static_cast<uint8_t>(results_at & 0xFF),
      static_cast<uint8_t>(results_at >> 8),
      0x08,
      0x68,
      0x8D,
      static_cast<uint8_t>((results_at + 1) & 0xFF),
      static_cast<uint8_t>(results_at >> 8),
      0x4C,
      static_cast<uint8_t>((program_start + 35) & 0xFF),
      static_cast<uint8_t>((program_start + 35) >> 8),
  };
  const uint16_t sentinel = program_start + 35;
  TestFixtures::ScopedCore::poke(program_start, program);
  TestFixtures::enter_at({program_start, 0, 0, 0});
  TestFixtures::step_until_pc(sentinel, call_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == sentinel);
  return CallResult{mem[results_at], mem[results_at + 1]};
}

// Boots block 0 through the card, which issues the STATUS that loads the
// count registers, then writes the ramp into block 3 of the scratch copy
// through the firmware and leaves a READ of it 100 bytes into read-out.
auto boot_write_ramp_and_start_reading(int slot, const std::string& image)
    -> void {
  insert(slot, 0, image);
  REQUIRE(status(slot).drive0_loaded == 1);
  linapple_reset_hard();
  TestFixtures::step_until_pc(boot_entry, boot_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == boot_entry);

  TestFixtures::ScopedCore::poke(write_buffer, ramp());
  const CallResult written = call_driver(
      slot, prodos_write, unit_for(slot, 0), write_buffer, ramp_block);
  REQUIRE_FALSE(written.carry());
  REQUIRE(written.a == prodos_ok);

  poke(io_base(slot) + 1, unit_for(slot, 0));
  poke(io_base(slot) + 2, ramp_block);
  poke(io_base(slot) + 3, 0);
  poke(io_base(slot) + 0, prodos_read);
  REQUIRE(peek(io_base(slot) + 0) == prodos_ok);
  for (uint16_t i = 0; i < taken; ++i) {
    REQUIRE(peek(io_base(slot) + 4) == (i & 0xFF));
  }
}

auto expected_frame(int slot, uint8_t phase) -> Frame {
  Frame frame = {
      {
          0x01, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x70, 0x01,
          0x00, 0x01, 0x03, 0x00, 0x64, 0x00, 0x10, 0x00, 0x00, 0x00,
      },
  };
  frame.at(8) = unit_for(slot, 0);
  frame.at(11) = phase;
  return frame;
}

// What the registers read back, with the data port left alone.
struct Registers {
  uint8_t result;
  uint8_t unit;
  uint16_t block;
  uint16_t count;
};

auto registers(int slot) -> Registers {
  Registers regs{};
  regs.result = peek(io_base(slot) + 0);
  regs.unit = peek(io_base(slot) + 1);
  regs.block = static_cast<uint16_t>(peek(io_base(slot) + 2) |
                                     (peek(io_base(slot) + 3) << 8));
  regs.count = static_cast<uint16_t>(peek(io_base(slot) + 5) |
                                     (peek(io_base(slot) + 6) << 8));
  return regs;
}

auto check_resumed_read_out(int slot) -> void {
  const Registers regs = registers(slot);
  CHECK(regs.result == prodos_ok);
  CHECK(regs.unit == unit_for(slot, 0));
  CHECK(regs.block == ramp_block);
  CHECK(regs.count == hdv_blocks);
  const std::array<uint8_t, block_size> bytes = ramp();
  for (size_t i = taken; i < block_size; ++i) {
    CAPTURE(i);
    CHECK(peek(io_base(slot) + 4) == bytes.at(i));
  }
}

}  // namespace

TEST_CASE(
    "Harddisk save state: the frame is the controller's twenty bytes of "
    "registers, a read-out in flight recorded as such, in slot 7 and in slot "
    "5") {
  static_assert(sizeof(HarddiskSaveState) == frame_size,
                "the frame is twenty bytes");
  static_assert(offsetof(HarddiskSaveState, unit) == 8 &&
                    offsetof(HarddiskSaveState, command) == 9 &&
                    offsetof(HarddiskSaveState, result) == 10 &&
                    offsetof(HarddiskSaveState, data_phase) == 11 &&
                    offsetof(HarddiskSaveState, block) == 12 &&
                    offsetof(HarddiskSaveState, data_index) == 14 &&
                    offsetof(HarddiskSaveState, block_count) == 16 &&
                    offsetof(HarddiskSaveState, reserved) == 18,
                "every field sits where a file written earlier put it");
  static_assert(sizeof(HarddiskSaveState) <= snapshot_slot_state_capacity,
                "the frame rides the slot trailer");

  for (const int slot : {7, 5}) {
    CAPTURE(slot);
    Machine machine(machine_with_card(slot));
    REQUIRE(peripheral_present(slot, harddisk_id));
    const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
    boot_write_ramp_and_start_reading(slot, image.path());

    size_t needed = 0;
    peripheral_save_state(slot, nullptr, &needed);
    CHECK(needed == frame_size);
    CHECK(saved_frame(slot) == expected_frame(slot, 1));
  }
}

TEST_CASE(
    "Harddisk save state: a write-in in flight is recorded as phase 2 with "
    "the index where the push stopped") {
  const int slot = 7;
  Machine machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  insert(slot, 0, image.path());
  linapple_reset_hard();
  TestFixtures::step_until_pc(boot_entry, boot_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == boot_entry);

  poke(io_base(slot) + 1, unit_for(slot, 0));
  poke(io_base(slot) + 2, ramp_block);
  poke(io_base(slot) + 3, 0);
  for (uint16_t i = 0; i < taken; ++i) {
    poke(io_base(slot) + 4, static_cast<uint8_t>(i));
  }
  Frame expected = expected_frame(slot, 2);
  // No READ ran, so the command register still holds the boot's last one.
  expected.at(9) = prodos_read;
  CHECK(saved_frame(slot) == expected);
}

TEST_CASE(
    "Harddisk save state: a real .aws round trip gives the registers back "
    "and re-reads the block in flight from the mounted image") {
  const int slot = 7;
  Machine machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  boot_write_ramp_and_start_reading(slot, image.path());

  TestFixtures::ScopedTempFile file(".aws");
  save_state_set_filename(file.c_str());
  {
    TestFixtures::ScopedLogCapture log;
    save_state_save();
    CHECK(log.count_containing("exceeds the") == 0);
  }

  linapple_reset_hard();
  CHECK(registers(slot).count == 0);
  CHECK(saved_frame(slot) != expected_frame(slot, 1));

  TestFixtures::ScopedLogCapture log;
  REQUIRE(save_state_load());
  CHECK(log.count_containing("resuming against drive 1") == 1);
  CHECK(log.count_containing(image.path()) == 1);
  CHECK(saved_frame(slot) == expected_frame(slot, 1));
  CHECK(status(slot).drive0_loaded == 1);
  check_resumed_read_out(slot);
}

TEST_CASE(
    "Harddisk save state: the pinned .aws fixture loads the same way against "
    "a freshly mounted image") {
  const int slot = 7;
  Machine machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  boot_write_ramp_and_start_reading(slot, image.path());
  linapple_reset_hard();

  const std::string path =
      TestFixtures::get_fixture_path("harddisk-read-out.aws");
  REQUIRE(access(path.c_str(), R_OK) == 0);
  save_state_set_filename(path.c_str());
  TestFixtures::ScopedLogCapture log;
  REQUIRE(save_state_load());
  CHECK(log.count_containing("resuming against drive 1") == 1);
  CHECK(saved_frame(slot) == expected_frame(slot, 1));
  check_resumed_read_out(slot);
}

TEST_CASE(
    "Harddisk save state: a file written before the card had a frame loads "
    "with the card at reset, its slot-7 entry refused once, and a READ then "
    "serves the mounted image") {
  const int slot = 7;
  Machine machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  boot_write_ramp_and_start_reading(slot, image.path());
  REQUIRE(saved_frame(slot) == expected_frame(slot, 1));

  const std::string path =
      TestFixtures::get_fixture_path("harddisk-81d55907.aws");
  REQUIRE(access(path.c_str(), R_OK) == 0);
  save_state_set_filename(path.c_str());
  TestFixtures::ScopedLogCapture log;
  REQUIRE(save_state_load());
  CHECK(log.count_containing("Slot 7: Harddisk refused the 16-byte "
                             "fixed-body region and stays at reset") == 1);
  CHECK(log.count_containing("resuming against") == 0);

  const Registers regs = registers(slot);
  CHECK(regs.result == prodos_ok);
  CHECK(regs.unit == 0);
  CHECK(regs.block == 0);
  CHECK(regs.count == 0);
  CHECK(status(slot).drive0_loaded == 1);

  const CallResult read =
      call_driver(slot, prodos_read, unit_for(slot, 0), write_buffer, 1);
  CHECK_FALSE(read.carry());
  CHECK(read.a == prodos_ok);
  for (size_t i = 0; i < block_size; ++i) {
    CAPTURE(i);
    CHECK(mem[write_buffer + i] == 0x01);
  }
}

TEST_CASE(
    "Harddisk save state: a frame that is null, short, of another version "
    "or size, or a 2,160-byte frame is refused with every register "
    "untouched") {
  const int slot = 7;
  Machine machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  boot_write_ramp_and_start_reading(slot, image.path());
  const Frame before = saved_frame(slot);

  CHECK(peripheral_load_state(slot, nullptr, frame_size) == peripheral_error);
  CHECK(saved_frame(slot) == before);

  Frame frame = expected_frame(slot, 1);
  CHECK(peripheral_load_state(slot, frame.data(), frame_size - 1) ==
        peripheral_error);
  CHECK(saved_frame(slot) == before);

  frame.at(0) = 2;
  CHECK(peripheral_load_state(slot, frame.data(), frame.size()) ==
        peripheral_error);
  CHECK(saved_frame(slot) == before);

  frame = expected_frame(slot, 1);
  frame.at(4) = 21;
  CHECK(peripheral_load_state(slot, frame.data(), frame.size()) ==
        peripheral_error);
  CHECK(saved_frame(slot) == before);

  std::vector<uint8_t> old_layout(2160, 0);
  old_layout.at(0) = 1;
  old_layout.at(4) = 0x70;
  old_layout.at(5) = 0x08;
  CHECK(peripheral_load_state(slot, old_layout.data(), old_layout.size()) ==
        peripheral_error);
  CHECK(saved_frame(slot) == before);

  frame = expected_frame(slot, 1);
  frame.at(11) = 3;
  CHECK(peripheral_load_state(slot, frame.data(), frame.size()) ==
        peripheral_error);
  CHECK(saved_frame(slot) == before);

  check_resumed_read_out(slot);
}

TEST_CASE(
    "Harddisk save state: a write in flight at the save is lost, so the "
    "next WRITE fails once with an I/O error and the one after succeeds") {
  const int slot = 7;
  Machine machine(machine_with_card(slot));
  REQUIRE(peripheral_present(slot, harddisk_id));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  insert(slot, 0, image.path());
  linapple_reset_hard();
  TestFixtures::step_until_pc(boot_entry, boot_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == boot_entry);

  Frame frame = expected_frame(slot, 2);
  frame.at(14) = 200;
  frame.at(15) = 0;
  TestFixtures::ScopedLogCapture log;
  REQUIRE(peripheral_load_state(slot, frame.data(), frame.size()) ==
          peripheral_ok);
  CHECK(log.count_containing("a block write in flight at the save was lost") ==
        1);
  for (int i = 0; i < 512; ++i) {
    CHECK(peek(io_base(slot) + 4) == 0);
  }

  TestFixtures::ScopedCore::poke(write_buffer, ramp());
  CallResult result = call_driver(slot, prodos_write, unit_for(slot, 0),
                                    write_buffer, ramp_block);
  CHECK(result.carry());
  CHECK(result.a == prodos_io_error);

  result = call_driver(slot, prodos_write, unit_for(slot, 0), write_buffer,
                       ramp_block);
  CHECK_FALSE(result.carry());
  CHECK(result.a == prodos_ok);
  CHECK(log.count_containing("a block write in flight at the save was lost") ==
        1);
}
