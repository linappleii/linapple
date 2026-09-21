// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "apple2/peripherals/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

constexpr int slot_6 = 6;
constexpr uint16_t io_stepper_phase_0_off = 0xC0E0;
constexpr uint16_t io_stepper_phase_0_on = 0xC0E1;
constexpr uint16_t io_motor_off_switch = 0xC0E8;
constexpr uint16_t io_motor_on_switch = 0xC0E9;
constexpr uint16_t io_drive_0_select = 0xC0EA;
constexpr uint16_t io_drive_1_select = 0xC0EB;
constexpr uint16_t io_read_write_switch = 0xC0EC;
constexpr uint16_t io_latch_switch = 0xC0ED;
constexpr uint16_t io_read_mode_switch = 0xC0EE;
constexpr uint16_t io_write_mode_switch = 0xC0EF;
constexpr uint8_t latch_bit = 0x80;
constexpr uint8_t address_prologue_1 = 0xD5;
constexpr uint8_t address_prologue_2 = 0xAA;
constexpr uint8_t address_prologue_3 = 0x96;
constexpr uint8_t address_epilogue = 0xDE;
constexpr uint8_t default_volume = 0xFE;

// The 4-and-4 pair carries a byte as two nibbles with the odd and even bits
// split across them, which is how an address field stays readable.
auto decode_4and4(uint8_t high, uint8_t low) -> uint8_t {
  return static_cast<uint8_t>(((high << 1U) | 1U) & low);
}

// A marker on the byte the video scanner fetches at probe_cycles, so a switch
// that answers with the bus can be told apart from one answering with the
// card's data register. Neither value can arrive from the other source.
constexpr uint32_t probe_cycles = 12345;
constexpr uint8_t bus_marker = 0x5A;
constexpr uint8_t register_marker = 0x3C;

auto mark_floating_bus() -> void {
  TestFixtures::ScopedCore_t::poke(
      video_get_scanner_address(nullptr, probe_cycles), &bus_marker, 1);
}

// Declared rather than inherited: with no configuration the slot fallbacks in
// peripheral_register_internal supply a printer, a Super Serial Card, a
// Mockingboard and a hard disk that nothing here touches.
using TestConfig_t = TestFixtures::ScopedTestConfig_t;

auto disk_ii_no_speed_statement() -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[5] = "Disk II";
  return description;
}

class DiskIoHarness_t {
 public:
  explicit DiskIoHarness_t(bool insert_default_disk = true) {
    machine_.load();
    linapple_init();
    peripheral_manager_init();

    linapple_register_peripherals();

    if (insert_default_disk) {
      disk_fixture_ = TestFixtures::create_ephemeral("Master.dsk");
      mount_disk(disk_fixture_.path(), 0);
      select_drive_0();
      power_motor_on();
      select_read_mode();

      // Ensure track 0 is primed into memory
      read_byte();

      // Put the head back at the index hole and start a fresh slice
      DiskSavedState_t state = get_saved_state();
      state.drives[0].current_byte_pos = 0;
      load_saved_state(state);
    }
  }

  ~DiskIoHarness_t() { linapple_shutdown(); }

  DiskIoHarness_t(const DiskIoHarness_t&) = delete;
  auto operator=(const DiskIoHarness_t&) -> DiskIoHarness_t& = delete;
  DiskIoHarness_t(DiskIoHarness_t&&) = delete;
  auto operator=(DiskIoHarness_t&&) -> DiskIoHarness_t& = delete;

  auto mount_disk(const std::string& path, int drive_idx = 0) -> void {
    DiskInsertCmd_t cmd{};
    cmd.drive = (drive_idx == 1) ? disk_drive_1 : disk_drive_0;
    cmd.write_protected = false;
    util_safe_strcpy(cmd.path, path.c_str(), disk_insert_path_max);
    peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
    peripheral_manager_think(0);
  }

  auto select_drive_0() -> void {
    io_map_dispatch(0, io_drive_0_select, 0, 0, 0);
  }

  auto eject_disk(int drive_idx = 0) -> void {
    DiskEjectCmd_t cmd{};
    cmd.drive = (drive_idx == 1) ? disk_drive_1 : disk_drive_0;
    peripheral_command(slot_6, disk_cmd_eject, &cmd, sizeof(cmd));
  }

  auto power_motor_on() -> void {
    io_map_dispatch(0, io_motor_on_switch, 0, 0, 0);
  }

  auto power_motor_off() -> void {
    io_map_dispatch(0, io_motor_off_switch, 0, 0, 0);
  }

  auto select_read_mode() -> uint8_t {
    return io_map_dispatch(0, io_read_mode_switch, 0, 0, 0);
  }

  auto select_write_mode() -> uint8_t {
    return io_map_dispatch(0, io_write_mode_switch, 0, 0, 0);
  }

  auto read_switch(uint16_t switch_address, uint32_t executed_cycles)
      -> uint8_t {
    return io_map_dispatch(0, switch_address, 0, 0, executed_cycles);
  }

  // The sequencer hands a byte over once, holds it for a few cells and then
  // clears it, so a caller after the next byte polls the way RWTS does:
  // wait for the register to drop, then wait for bit 7 to come back.
  auto read_byte() -> uint8_t {
    constexpr uint32_t poll_step = 2;
    constexpr uint32_t slice_limit = 4096;
    constexpr int poll_limit = 40000;
    for (int poll = 0; poll < poll_limit; ++poll) {
      slice_cycle_ += poll_step;
      const uint8_t value =
          io_map_dispatch(0, io_read_write_switch, 0, 0, slice_cycle_);
      if (slice_cycle_ >= slice_limit) {
        end_slice();
      }
      if ((value & latch_bit) == 0) {
        register_is_clear_ = true;
        continue;
      }
      if (register_is_clear_) {
        register_is_clear_ = false;
        return value;
      }
    }
    return 0;
  }

  auto end_slice() -> void {
    peripheral_manager_think(slice_cycle_);
    slice_cycle_ = 0;
  }

  auto write_latch(uint8_t val) -> uint8_t {
    return io_map_dispatch(0, io_latch_switch, 1, val, 0);
  }

  auto think(uint32_t cycles) -> void { peripheral_manager_think(cycles); }

  auto save_state(DiskSavedState_t& out_state) const -> void {
    size_t size = sizeof(out_state);
    peripheral_save_state(slot_6, &out_state, &size);
  }

  auto get_saved_state() const -> DiskSavedState_t {
    DiskSavedState_t state{};
    save_state(state);
    return state;
  }

  auto load_saved_state(const DiskSavedState_t& in_state) -> void {
    peripheral_load_state(slot_6, &in_state, sizeof(in_state));
  }

  auto spin(uint32_t cycles) -> void {
    end_slice();
    peripheral_manager_think(cycles);
  }

 private:
  TestConfig_t machine_{disk_ii_no_speed_statement()};
  TestFixtures::EphemeralDiskFixture_t disk_fixture_;
  uint32_t slice_cycle_ = 0;
  bool register_is_clear_ = true;
};

}  // namespace

TEST_CASE("DiskIO: [IO-01] Sequential Read") {
  DiskIoHarness_t harness;

  harness.select_read_mode();

  // Long enough to pass a whole sector: fifteen address bytes, a gap, a data
  // field of 343 and a second gap.
  constexpr size_t read_count = 1024;
  std::vector<uint8_t> stream;
  stream.reserve(read_count);
  for (size_t i = 0; i < read_count; ++i) {
    const uint8_t byte_read = harness.read_byte();

    // Physical Apple II GCR contract: every valid floppy nibble has bit 7 set
    CHECK((byte_read & latch_bit) != 0);
    stream.push_back(byte_read);
  }

  // What the 6502 must be able to do with the stream: find an address field
  // and read the track it is standing on back out of it.
  constexpr size_t address_field_len = 12;
  bool found_address_field = false;
  for (size_t i = 0; i + address_field_len < stream.size(); ++i) {
    if (stream[i] != address_prologue_1 ||
        stream[i + 1] != address_prologue_2 ||
        stream[i + 2] != address_prologue_3) {
      continue;
    }
    const uint8_t volume = decode_4and4(stream[i + 3], stream[i + 4]);
    const uint8_t track = decode_4and4(stream[i + 5], stream[i + 6]);
    const uint8_t sector = decode_4and4(stream[i + 7], stream[i + 8]);
    const uint8_t checksum = decode_4and4(stream[i + 9], stream[i + 10]);

    CHECK(volume == default_volume);
    CHECK(track == 0);
    CHECK(sector < sectors_per_track);
    CHECK(checksum == (volume ^ track ^ sector));
    CHECK(stream[i + 11] == address_epilogue);
    found_address_field = true;
    break;
  }
  CHECK(found_address_field);
}

TEST_CASE("DiskIO: [IO-02] Spindle Rotation") {
  DiskIoHarness_t harness;

  harness.select_read_mode();

  const uint8_t b_start = harness.read_byte();
  harness.end_slice();
  const DiskSavedState_t state_before = harness.get_saved_state();
  const int32_t pos_before = state_before.drives[0].current_byte_pos;

  // The medium is clocked in 125 ns units, four to a sequencer step and two
  // steps to a cycle, so at the default cell timing of 32 units these cycles
  // carry 20000 * 8 / 32 = 5000 cells, which is 625 whole byte positions.
  constexpr uint32_t elapsed_cycles = 20000;
  constexpr int32_t expected_advance = 625;
  harness.spin(elapsed_cycles);

  const int32_t expected_pos = pos_before + expected_advance;

  const DiskSavedState_t state_after = harness.get_saved_state();
  CHECK(state_after.drives[0].current_byte_pos == expected_pos);

  // The head has moved on, so the byte under it is a different one.
  const uint8_t b_after = harness.read_byte();
  CHECK((b_after & latch_bit) != 0);
  CHECK(b_after != b_start);
}

TEST_CASE("DiskIO: [IO-03] Floating Bus Accuracy") {
  // Construct harness with no disk loaded in drive 0
  DiskIoHarness_t harness(false);

  harness.power_motor_on();
  harness.select_read_mode();

  // With no disk inserted, accessing slot 6 I/O ($C0EC) returns floating bus
  // noise with bit 7 set
  constexpr size_t test_reads = 16;
  for (size_t i = 0; i < test_reads; ++i) {
    const uint8_t noise = harness.read_byte();
    CHECK((noise & latch_bit) != 0);
  }
}

TEST_CASE("DiskIO: [IO-04] Latch Persistence") {
  DiskIoHarness_t harness;
  mark_floating_bus();

  // Loading the register is the sequencer's job, so the byte the 6502 puts on
  // the bus lands a few cells later rather than at the access itself.
  constexpr uint32_t settle_cycles = 64;
  constexpr uint8_t pattern_55 = 0x55;
  constexpr uint8_t pattern_aa = 0xAA;

  harness.select_write_mode();
  harness.write_latch(pattern_55);
  harness.think(settle_cycles);

  // $C0ED is odd, so the register it loads never reaches the data bus
  CHECK(harness.read_switch(io_latch_switch, probe_cycles) == bus_marker);

  DiskSavedState_t state_55 = harness.get_saved_state();
  CHECK(state_55.io_latch == pattern_55);
  CHECK(state_55.is_write_mode != 0);

  harness.write_latch(pattern_aa);
  harness.think(settle_cycles);
  CHECK(harness.read_switch(io_latch_switch, probe_cycles) == bus_marker);

  DiskSavedState_t state_aa = harness.get_saved_state();
  CHECK(state_aa.io_latch == pattern_aa);

  // Sensing write protect at $C0EE takes the register over. The shift-right
  // command feeds the protect line in on every step, so the answer is
  // saturated either way and never a partial byte, whatever the fixture's
  // permissions are.
  harness.select_read_mode();
  harness.think(settle_cycles);
  const uint8_t protect_sense =
      harness.read_switch(io_read_write_switch, probe_cycles);
  CHECK((protect_sense == 0x00 || protect_sense == 0xFF));

  DiskSavedState_t state_read_mode = harness.get_saved_state();
  CHECK(state_read_mode.io_latch == protect_sense);
  CHECK(state_read_mode.is_write_mode == 0);
}

TEST_CASE("DiskIO: [IO-21] Odd switches read the bus the card sits on") {
  DiskIoHarness_t harness(false);
  mark_floating_bus();

  // A0 is high on these four, so the data register stays off the bus and what
  // the 6502 reads is whatever the video scanner is fetching on that cycle.
  REQUIRE(mem_read_floating_bus(probe_cycles) == bus_marker);

  CHECK(harness.read_switch(io_stepper_phase_0_on, probe_cycles) == bus_marker);
  CHECK(harness.read_switch(io_motor_on_switch, probe_cycles) == bus_marker);
  CHECK(harness.read_switch(io_drive_1_select, probe_cycles) == bus_marker);
  CHECK(harness.read_switch(io_write_mode_switch, probe_cycles) == bus_marker);
}

TEST_CASE("DiskIO: [IO-22] Even switches answer with the data register") {
  DiskIoHarness_t harness(false);
  mark_floating_bus();

  // Holding the card in load mode makes the register a latch again: every
  // sequencer step reloads it from the same byte, so a probe can tell a
  // register answer apart from the bus or a constant.
  harness.power_motor_on();
  harness.select_write_mode();
  harness.write_latch(register_marker);
  harness.read_switch(io_latch_switch, probe_cycles);

  constexpr size_t repeat_count = 32;
  for (size_t i = 0; i < repeat_count; ++i) {
    CHECK(harness.read_switch(io_stepper_phase_0_off, probe_cycles) ==
          register_marker);
    CHECK(harness.read_switch(io_motor_off_switch, probe_cycles) ==
          register_marker);
    CHECK(harness.read_switch(io_drive_0_select, probe_cycles) ==
          register_marker);
  }
}

TEST_CASE("DiskIO: [IO-23] The read-mode switch drives the write-protect bit") {
  DiskIoHarness_t harness(false);
  mark_floating_bus();

  harness.power_motor_on();
  harness.select_write_mode();
  harness.write_latch(register_marker);
  uint32_t cycle = probe_cycles;
  harness.read_switch(io_latch_switch, cycle);
  CHECK(harness.read_switch(io_stepper_phase_0_off, cycle) == register_marker);

  // $C0EE leaves Q6 set, which is the shift-right command, so the marker is
  // driven out of the register a cell at a time and replaced by the protect
  // sense of an empty drive.
  cycle += 64;
  harness.read_switch(io_read_mode_switch, cycle);
  cycle += 64;
  CHECK(harness.read_switch(io_stepper_phase_0_off, cycle) == 0x00);
  cycle += 64;
  CHECK(harness.read_switch(io_stepper_phase_0_off, cycle) == 0x00);
}
