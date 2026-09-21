// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <string>

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

      // Reset head byte position and access flag to clean initial baseline
      DiskSavedState_t state = get_saved_state();
      state.drives[0].current_byte_pos = 0;
      state.was_accessed_this_tick = 0;
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

  auto clear_accessed_flag() -> void {
    DiskSavedState_t state = get_saved_state();
    state.was_accessed_this_tick = 0;
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

  const DiskSavedState_t initial_state = harness.get_saved_state();
  const int32_t start_pos = initial_state.drives[0].current_byte_pos;
  const int32_t nibble_count = initial_state.drives[0].nibble_count;
  REQUIRE(nibble_count > 0);

  // Read a burst of sequential nibbles from $C0EC
  constexpr size_t read_count = 32;
  for (size_t i = 0; i < read_count; ++i) {
    const int32_t expected_pos =
        static_cast<int32_t>((start_pos + i) % nibble_count);
    const uint8_t expected_nibble =
        initial_state.drives[0].track_buffer[expected_pos];

    const uint8_t byte_read = harness.read_byte();

    // Physical Apple II GCR contract: every valid floppy nibble has bit 7 set
    CHECK((byte_read & latch_bit) != 0);

    // Exact nibble match against the physical track buffer
    CHECK(byte_read == expected_nibble);
  }

  // Verify that the head position advanced sequentially by exactly read_count
  const DiskSavedState_t final_state = harness.get_saved_state();
  const int32_t expected_final_pos =
      static_cast<int32_t>((start_pos + read_count) % nibble_count);
  CHECK(final_state.drives[0].current_byte_pos == expected_final_pos);
}

TEST_CASE("DiskIO: [IO-02] Spindle Rotation") {
  DiskIoHarness_t harness;

  harness.select_read_mode();

  // Read initial byte b_start and record initial head byte position
  const uint8_t b_start = harness.read_byte();
  const DiskSavedState_t state_before = harness.get_saved_state();
  const int32_t pos_before = state_before.drives[0].current_byte_pos;
  const int32_t nibble_count = state_before.drives[0].nibble_count;
  REQUIRE(nibble_count > 0);

  // Clear single-cycle access latch to simulate completion of the read cycle
  harness.clear_accessed_flag();

  // Advance virtual spindle by spinning virtual cycles (20,000 cycles via
  // think)
  constexpr uint32_t elapsed_cycles = 20000;
  harness.think(elapsed_cycles);

  // Assert that current_byte_pos has deterministically advanced by 20000 >> 5
  // (625 nibbles)
  constexpr int32_t expected_advance =
      static_cast<int32_t>(elapsed_cycles >> 5);
  const int32_t expected_pos = (pos_before + expected_advance) % nibble_count;

  const DiskSavedState_t state_after = harness.get_saved_state();
  CHECK(state_after.drives[0].current_byte_pos == expected_pos);

  // Assert the byte read from $C0EC equals expected nibble at that position and
  // differs from b_start
  const uint8_t expected_nibble =
      state_after.drives[0].track_buffer[expected_pos];
  const uint8_t b_after = harness.read_byte();
  CHECK(b_after == expected_nibble);
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

  // Sensing write protect at $C0EE loads the register, so the pattern does not
  // survive the mode switch: the register holds the protect bit and nothing
  // else, whatever the fixture's permissions are
  const uint8_t protect_sense = harness.select_read_mode();
  CHECK((protect_sense & static_cast<uint8_t>(~latch_bit)) == 0);

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

  harness.write_latch(register_marker);

  // $C0EE is the one even switch that loads the register itself, so it is the
  // control that shows the other three are not returning a stale marker.
  CHECK(harness.read_switch(io_read_mode_switch, probe_cycles) == 0x00);
  CHECK(harness.read_switch(io_stepper_phase_0_off, probe_cycles) == 0x00);
}
