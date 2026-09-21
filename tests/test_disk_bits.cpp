// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>

#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr int slot_6 = 6;
constexpr uint16_t io_phase_1_on = 0xC0E3;
constexpr uint16_t io_phase_1_off = 0xC0E2;
constexpr uint16_t io_phase_2_on = 0xC0E5;
constexpr uint16_t io_phase_2_off = 0xC0E4;
constexpr uint16_t io_motor_on = 0xC0E9;
constexpr uint16_t io_q6_clear = 0xC0EC;
constexpr uint16_t io_q7_clear = 0xC0EE;

// The medium is clocked in 125 ns units, four to a sequencer step and two
// steps to a CPU cycle, so at the nominal cell time of 32 units one cell
// takes four cycles and one 6502 second of 1,020,484 cycles carries
// 1020484 * 8 / 32 cells. Against a 50,464-cell track that is 5.0555
// revolutions a second, or 303 rpm as the emulated clock measures it: two
// per cent over a real drive's 297, which is the stretched cycle showing.
constexpr uint32_t cycles_per_emulated_second = 1020484;
constexpr uint32_t cells_per_emulated_second = 255121;
constexpr uint32_t cycles_per_cell = 4;
constexpr uint32_t cells_per_byte = 8;

// The synthesised track the sector adapter builds: sixteen sectors and a
// forty-eight byte gap 1.
constexpr uint32_t nominal_track_cells = 50464;

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

class DiskBitsHarness_t {
 public:
  DiskBitsHarness_t() {
    machine_.load();
    linapple_init();
    peripheral_manager_init();
    linapple_register_peripherals();

    disk_ = TestFixtures::create_ephemeral("minimal.dsk");

    DiskInsertCmd_t cmd{};
    cmd.drive = disk_drive_0;
    util_safe_strcpy(cmd.path, disk_.path().c_str(), disk_insert_path_max);
    peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
    peripheral_manager_think(0);

    io_map_dispatch(0, io_motor_on, 0, 0, 0);
    io_map_dispatch(0, io_q7_clear, 0, 0, 0);
    io_map_dispatch(0, io_q6_clear, 0, 0, 0);
    peripheral_manager_think(1);
  }

  ~DiskBitsHarness_t() { linapple_shutdown(); }

  DiskBitsHarness_t(const DiskBitsHarness_t&) = delete;
  auto operator=(const DiskBitsHarness_t&) -> DiskBitsHarness_t& = delete;

  auto read(uint16_t address, uint32_t cycle) -> uint8_t {
    return io_map_dispatch(0, address, 0, 0, cycle);
  }

  auto spin(uint32_t cycles) -> void { peripheral_manager_think(cycles); }

  auto state() const -> DiskSavedState_t {
    DiskSavedState_t saved{};
    size_t size = sizeof(saved);
    peripheral_save_state(slot_6, &saved, &size);
    return saved;
  }

  auto byte_position() const -> int32_t {
    return state().drives[0].current_byte_pos;
  }

  auto park_at_index_hole() -> void {
    DiskSavedState_t saved = state();
    saved.drives[0].current_byte_pos = 0;
    peripheral_load_state(slot_6, &saved, sizeof(saved));
    io_map_dispatch(0, io_motor_on, 0, 0, 0);
  }

 private:
  TestConfig_t machine_{TestConfig_t::disk_ii_only()};
  TestFixtures::EphemeralDiskFixture_t disk_;
};

}  // namespace

TEST_CASE("DiskBits: [BITS-01] One emulated second carries 255,121 cells") {
  DiskBitsHarness_t harness;
  harness.park_at_index_hole();
  REQUIRE(harness.byte_position() == 0);

  harness.spin(cycles_per_emulated_second);

  // The head laps the track five times and stops a fifth of the way round.
  constexpr uint32_t landed_cell =
      cells_per_emulated_second % nominal_track_cells;
  CHECK(harness.byte_position() ==
        static_cast<int32_t>(landed_cell / cells_per_byte));
}

TEST_CASE("DiskBits: [BITS-02] A 50,464-cell track wraps exactly") {
  DiskBitsHarness_t harness;
  harness.park_at_index_hole();
  REQUIRE(harness.byte_position() == 0);

  harness.spin(nominal_track_cells * cycles_per_cell);
  CHECK(harness.byte_position() == 0);

  // A single cell short of a lap leaves the head at the last byte rather
  // than back at the index hole.
  harness.park_at_index_hole();
  harness.spin((nominal_track_cells - cells_per_byte) * cycles_per_cell);
  CHECK(harness.byte_position() ==
        static_cast<int32_t>(nominal_track_cells / cells_per_byte) - 1);
}

TEST_CASE("DiskBits: [BITS-03] Reads 32 cycles apart are a byte apart") {
  DiskBitsHarness_t harness;
  harness.park_at_index_hole();

  constexpr uint32_t cycles_per_nibble = cells_per_byte * cycles_per_cell;
  harness.read(io_q6_clear, cycles_per_nibble);
  const int32_t first = harness.byte_position();
  harness.read(io_q6_clear, cycles_per_nibble * 2);
  const int32_t second = harness.byte_position();

  CHECK(second - first == 1);
}

TEST_CASE("DiskBits: [BITS-04] A head step keeps the angle it left on") {
  DiskBitsHarness_t harness;
  harness.park_at_index_hole();

  constexpr uint32_t travel_cycles = 4000;
  harness.spin(travel_cycles);
  const int32_t before = harness.byte_position();
  REQUIRE(before > 0);

  // Two magnet strobes take the head to the next whole track, each held long
  // enough for the coil to pull; the next track is the same length, so the
  // angle carries across but for the cells that pass while the arm moves.
  constexpr uint32_t hold_cycles = 100;
  constexpr uint32_t strobes = 4;
  for (const uint16_t strobe :
       {io_phase_1_on, io_phase_1_off, io_phase_2_on, io_phase_2_off}) {
    harness.read(strobe, 0);
    harness.spin(hold_cycles);
  }

  // Reading is what pulls the new track in, and the angle comes with it.
  harness.read(io_q6_clear, 1);
  harness.spin(2);

  constexpr int32_t travelled_bytes = static_cast<int32_t>(
      strobes * hold_cycles / cycles_per_cell / cells_per_byte);
  const DiskSavedState_t after = harness.state();
  CHECK(after.drives[0].track == 1);
  CHECK(after.drives[0].current_byte_pos > before);
  CHECK(after.drives[0].current_byte_pos <= before + travelled_bytes + 2);
}
