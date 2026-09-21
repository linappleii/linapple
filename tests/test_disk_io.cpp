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

  auto read_byte() -> uint8_t {
    return io_map_dispatch(0, io_read_write_switch, 0, 0, 0);
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

  // The within-slice access mark is a transient the save state does not
  // carry, so a round trip through it starts the slice over.
  auto clear_accessed_flag() -> void {
    const DiskSavedState_t state = get_saved_state();
    load_saved_state(state);
  }

 private:
  TestConfig_t machine_{disk_ii_no_speed_statement()};
  TestFixtures::EphemeralDiskFixture_t disk_fixture_;
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

  // Read initial byte b_start and record initial head byte position
  const uint8_t b_start = harness.read_byte();
  const DiskSavedState_t state_before = harness.get_saved_state();
  const int32_t pos_before = state_before.drives[0].current_byte_pos;

  // Clear single-cycle access latch to simulate completion of the read cycle
  harness.clear_accessed_flag();

  // Advance virtual spindle by spinning virtual cycles (20,000 cycles via
  // think)
  constexpr uint32_t elapsed_cycles = 20000;
  harness.think(elapsed_cycles);

  // Assert that current_byte_pos has deterministically advanced by 20000 >> 5
  // (625 bytes). One revolution is 6308 bytes, so the head cannot lap the
  // index hole and wrap inside this window.
  constexpr int32_t expected_advance =
      static_cast<int32_t>(elapsed_cycles >> 5);
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

  // Switch to write mode ($C0EF)
  harness.select_write_mode();

  // Write alternating bit pattern 0x55 to latch ($C0ED)
  constexpr uint8_t pattern_55 = 0x55;
  const uint8_t write_ret_55 = harness.write_latch(pattern_55);
  CHECK(write_ret_55 == pattern_55);

  // $C0ED is odd, so the register it just loaded never reaches the data bus
  CHECK(harness.read_switch(io_latch_switch, probe_cycles) == bus_marker);

  // Verify internal latch state reflects 0x55
  DiskSavedState_t state_55 = harness.get_saved_state();
  CHECK(state_55.io_latch == pattern_55);
  CHECK(state_55.is_write_mode != 0);

  // Write complementary bit pattern 0xAA to latch ($C0ED)
  constexpr uint8_t pattern_aa = 0xAA;
  const uint8_t write_ret_aa = harness.write_latch(pattern_aa);
  CHECK(write_ret_aa == pattern_aa);

  CHECK(harness.read_switch(io_latch_switch, probe_cycles) == bus_marker);

  // Verify internal latch state reflects 0xAA
  DiskSavedState_t state_aa = harness.get_saved_state();
  CHECK(state_aa.io_latch == pattern_aa);

  // Sensing write protect at $C0EE loads the register, so the pattern does
  // not survive the mode switch. The shift-right command feeds the protect
  // line in on every step, so the answer is saturated either way and never a
  // partial byte, whatever the fixture's permissions are.
  const uint8_t protect_sense = harness.select_read_mode();
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

  // Loading the register from the bus is the Q7-and-Q6 function, so the
  // marker only lands with the drive enabled and write mode selected.
  harness.power_motor_on();
  harness.select_write_mode();
  harness.write_latch(register_marker);

  // None of these three touches the register, so all three answer with the
  // byte it already holds rather than with the bus or a constant. Repeating
  // the read shows the answer is a latch, not the pseudo-random approximation
  // this replaced.
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

  // $C0EE is the one even switch that loads the register itself, so it is the
  // control that shows the other three are not returning a stale marker.
  CHECK(harness.read_switch(io_read_mode_switch, probe_cycles) == 0x00);
  CHECK(harness.read_switch(io_stepper_phase_0_off, probe_cycles) == 0x00);
}
