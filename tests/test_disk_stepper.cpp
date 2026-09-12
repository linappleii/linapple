// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr int slot_6 = 6;
constexpr uint16_t stepper_base = 0xC0E0;
constexpr uint16_t motor_off_switch = 0xC0E8;
constexpr uint16_t motor_on_switch = 0xC0E9;
constexpr uint16_t drive0_select_switch = 0xC0EA;
constexpr uint16_t read_write_switch = 0xC0EC;
constexpr uint16_t latch_switch = 0xC0ED;
constexpr uint16_t read_mode_switch = 0xC0EE;
constexpr uint16_t write_mode_switch = 0xC0EF;
constexpr uint64_t spin_settle_cycles = 10000;
constexpr uint32_t motor_spindown_cycles = 1500000;
constexpr size_t track_size_bytes = 4096;

class DiskStepperHarness_t {
 public:
  explicit DiskStepperHarness_t(const std::string& fixture_name = "Master.dsk")
      : disk_fixture_(TestFixtures::create_ephemeral(fixture_name)) {
    linapple_init();
    peripheral_manager_init();
    linapple_register_peripherals();

    mount_disk();
    spin_up();
  }

  ~DiskStepperHarness_t() { linapple_shutdown(); }

  DiskStepperHarness_t(const DiskStepperHarness_t&) = delete;
  auto operator=(const DiskStepperHarness_t&) -> DiskStepperHarness_t& = delete;
  DiskStepperHarness_t(DiskStepperHarness_t&&) = delete;
  auto operator=(DiskStepperHarness_t&&) -> DiskStepperHarness_t& = delete;

  auto step_phase(int phase, bool on) -> void {
    const uint16_t addr = static_cast<uint16_t>(
        stepper_base + ((phase & 0x03) * 2) + (on ? 1 : 0));
    io_map_dispatch(0, addr, 0, 0, 0);
    peripheral_manager_think(0);
  }

  auto release_all_phases() -> void {
    for (int p = 0; p < 4; ++p) {
      step_phase(p, false);
    }
  }

  auto step_forward_phase() -> void {
    const int32_t cur = get_phase();
    const int next_magnet = (cur + 1) & 0x03;
    release_all_phases();
    step_phase(next_magnet, true);
  }

  auto step_backward_phase() -> void {
    const int32_t cur = get_phase();
    const int prev_magnet = (cur + 3) & 0x03;
    release_all_phases();
    step_phase(prev_magnet, true);
  }

  auto step_to_track(int target_track) -> void {
    const int target_phase = target_track * phases_per_track;
    int safety_limit = max_disk_phases * 2;
    while (get_phase() < target_phase && safety_limit-- > 0) {
      step_forward_phase();
    }
    while (get_phase() > target_phase && safety_limit-- > 0) {
      step_backward_phase();
    }
  }

  auto read_data() -> uint8_t {
    return io_map_dispatch(0, read_write_switch, 0, 0, 0);
  }

  auto write_data(uint8_t val) -> void {
    io_map_dispatch(0, latch_switch, 1, val, 0);
    io_map_dispatch(0, read_write_switch, 0, 0, 0);
  }

  auto set_read_mode() -> void {
    io_map_dispatch(0, read_mode_switch, 0, 0, 0);
  }

  auto power_motor_on() -> void {
    io_map_dispatch(0, motor_on_switch, 0, 0, 0);
  }

  auto power_motor_off() -> void {
    io_map_dispatch(0, motor_off_switch, 0, 0, 0);
  }

  auto think(uint32_t cycles) -> void { peripheral_manager_think(cycles); }

  auto set_write_mode() -> void {
    io_map_dispatch(0, write_mode_switch, 0, 0, 0);
  }

  auto save_state(DiskSavedState_t& out_state) const -> void {
    size_t size = sizeof(out_state);
    peripheral_save_state(slot_6, &out_state, &size);
  }

  auto get_saved_state() const -> DiskSavedState_t {
    DiskSavedState_t state{};
    save_state(state);
    return state;
  }

  auto get_phase() const -> int32_t {
    return get_saved_state().drives[0].phase;
  }

  auto get_track() const -> int32_t {
    return get_saved_state().drives[0].track;
  }

  auto is_dirty() const -> bool {
    return get_saved_state().drives[0].is_dirty != 0;
  }

  auto get_spinning_ticks() const -> uint32_t {
    return get_saved_state().drives[0].spinning_ticks;
  }

  auto fixture_path() const -> const std::string& {
    return disk_fixture_.path();
  }

 private:
  auto mount_disk() -> void {
    DiskInsertCmd_t cmd{};
    cmd.drive = static_cast<uint8_t>(disk_drive_0);
    cmd.write_protected = 0;
    util_safe_strcpy(cmd.path, disk_fixture_.c_str(), disk_insert_path_max);
    peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
    peripheral_manager_think(0);
  }

  auto spin_up() -> void {
    io_map_dispatch(0, drive0_select_switch, 0, 0, 0);
    io_map_dispatch(0, motor_on_switch, 0, 0, 0);
    io_map_dispatch(0, read_mode_switch, 0, 0, 0);
    peripheral_manager_think(spin_settle_cycles);
  }

  TestFixtures::EphemeralDiskFixture_t disk_fixture_;
};

auto read_disk_track_bytes(const std::string& file_path, int track_index)
    -> std::vector<uint8_t> {
  std::ifstream file(file_path, std::ios::binary);
  REQUIRE(file.is_open());
  const auto offset =
      static_cast<std::streamoff>(track_index * track_size_bytes);
  file.seekg(offset, std::ios::beg);
  std::vector<uint8_t> buffer(track_size_bytes, 0);
  file.read(reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(track_size_bytes));
  REQUIRE(file.gcount() == static_cast<std::streamsize>(track_size_bytes));
  return buffer;
}

}  // namespace

TEST_CASE("DiskStepper: [STEP-01] Phase to Track Mapping") {
  DiskStepperHarness_t harness;

  // Verify initial state: head begins at phase 0, track 0
  CHECK(harness.get_phase() == 0);
  CHECK(harness.get_track() == 0);

  // Read Track 0 data bytes from $C0EC
  std::vector<uint8_t> track0_bytes;
  track0_bytes.reserve(16);
  for (int i = 0; i < 16; ++i) {
    track0_bytes.push_back(harness.read_data());
  }
  for (uint8_t byte : track0_bytes) {
    CHECK((byte & 0x80) != 0);
  }

  // Step 1: Energize Phase 1 (moves to half-track 0.5, track 0)
  harness.step_phase(1, true);
  CHECK(harness.get_phase() == 1);
  CHECK(harness.get_track() == 0);

  // Step 2: Energize Phase 2, De-energize Phase 1 (moves to phase 2, track 1)
  harness.step_phase(2, true);
  harness.step_phase(1, false);
  CHECK(harness.get_phase() == 2);
  CHECK(harness.get_track() == 1);

  // Save state and verify head position in saved state ABI
  DiskSavedState_t state{};
  harness.save_state(state);
  CHECK(state.drives[0].phase == 2);
  CHECK(state.drives[0].track == 1);

  // Read Track 1 data bytes from $C0EC and verify they differ from Track 0
  std::vector<uint8_t> track1_bytes;
  track1_bytes.reserve(16);
  for (int i = 0; i < 16; ++i) {
    track1_bytes.push_back(harness.read_data());
  }
  for (uint8_t byte : track1_bytes) {
    CHECK((byte & 0x80) != 0);
  }
  CHECK(track0_bytes != track1_bytes);

  // Step to Track 2: Energize Phase 3, De-energize Phase 2, Energize Phase 0,
  // De-energize Phase 3
  harness.step_phase(3, true);
  harness.step_phase(2, false);
  CHECK(harness.get_phase() == 3);
  CHECK(harness.get_track() == 1);

  harness.step_phase(0, true);
  harness.step_phase(3, false);
  CHECK(harness.get_phase() == 4);
  CHECK(harness.get_track() == 2);

  harness.save_state(state);
  CHECK(state.drives[0].phase == 4);
  CHECK(state.drives[0].track == 2);

  std::vector<uint8_t> track2_bytes;
  track2_bytes.reserve(16);
  for (int i = 0; i < 16; ++i) {
    track2_bytes.push_back(harness.read_data());
  }
  for (uint8_t byte : track2_bytes) {
    CHECK((byte & 0x80) != 0);
  }
  CHECK(track2_bytes != track1_bytes);
  CHECK(track2_bytes != track0_bytes);
}

TEST_CASE("DiskStepper: [STEP-02] Track Clamping") {
  DiskStepperHarness_t harness;

  CHECK(harness.get_phase() == 0);
  CHECK(harness.get_track() == 0);

  // Step backward from Track 0 (energize Phase 3)
  harness.step_phase(3, true);
  CHECK(harness.get_phase() == 0);
  CHECK(harness.get_track() == 0);

  harness.step_phase(3, false);
  CHECK(harness.get_phase() == 0);
  CHECK(harness.get_track() == 0);

  // Step forward beyond track capacity (80 phases)
  for (int i = 0; i < max_disk_phases; ++i) {
    harness.step_forward_phase();
  }

  // Clamped at physical/emulated limit
  CHECK(harness.get_phase() == max_disk_phases - 1);
  CHECK(harness.get_track() == tracks_per_disk - 1);

  DiskSavedState_t state{};
  harness.save_state(state);
  CHECK(state.drives[0].phase == max_disk_phases - 1);
  CHECK(state.drives[0].track == tracks_per_disk - 1);

  // Step backward 80 phases
  for (int i = 0; i < max_disk_phases; ++i) {
    harness.step_backward_phase();
  }

  // Head returns to and clamps at track 0 / phase 0
  CHECK(harness.get_phase() == 0);
  CHECK(harness.get_track() == 0);

  harness.save_state(state);
  CHECK(state.drives[0].phase == 0);
  CHECK(state.drives[0].track == 0);
}

TEST_CASE("DiskStepper: [STEP-03] Flush on Seek") {
  DiskStepperHarness_t harness;

  CHECK(harness.get_phase() == 0);
  CHECK(harness.get_track() == 0);
  CHECK(harness.is_dirty() == false);

  // 1. Enter Write Mode
  harness.set_write_mode();
  {
    DiskSavedState_t state{};
    harness.save_state(state);
    CHECK(state.is_write_mode == 1);
  }

  // 2. Write unique byte pattern to Track 0
  constexpr uint8_t unique_byte = 0xA5;
  harness.write_data(unique_byte);

  // Track must be marked dirty prior to seek
  CHECK(harness.is_dirty() == true);
  {
    DiskSavedState_t state{};
    harness.save_state(state);
    CHECK(state.drives[0].is_dirty == 1);
    CHECK(state.io_latch == unique_byte);
  }

  // 3. Step head to Track 1
  harness.step_to_track(1);
  CHECK(harness.get_track() == 1);

  // Stepping the head must flush the dirty track to the driver/file and clear
  // dirty state
  CHECK(harness.is_dirty() == false);
  {
    DiskSavedState_t state{};
    harness.save_state(state);
    CHECK(state.drives[0].is_dirty == 0);
    CHECK(state.drives[0].track == 1);
  }

  // 4. Return to Read Mode and step back to Track 0
  harness.set_read_mode();
  harness.step_to_track(0);
  CHECK(harness.get_track() == 0);
  CHECK(harness.is_dirty() == false);

  // Query driver to verify healthy status following flushed write
  DiskStatus_t status{};
  size_t status_size = sizeof(status);
  peripheral_query(slot_6, disk_cmd_get_status, &status, &status_size);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == disk_err_none);
}

TEST_CASE(
    "DiskStepper: [STEP-04] Cylinder Boundary Seek Preserves Adjacent Track") {
  DiskStepperHarness_t harness("Master.dsk");

  // Baseline read of Track 16 and Track 17 contents from disk image file.
  // Track 17 contains VTOC and Catalog sectors; Track 16 contains file data
  // (e.g. HELLO).
  const std::vector<uint8_t> orig_track16 =
      read_disk_track_bytes(harness.fixture_path(), 16);
  const std::vector<uint8_t> orig_track17 =
      read_disk_track_bytes(harness.fixture_path(), 17);
  REQUIRE(orig_track16 != orig_track17);

  // 1. Step head to Track 17 (Phase 34)
  harness.step_to_track(17);
  CHECK(harness.get_phase() == 34);
  CHECK(harness.get_track() == 17);
  CHECK(harness.is_dirty() == false);

  // 2. Prime Track 17 into the peripheral track buffer
  const uint8_t prime_byte = harness.read_data();
  CHECK((prime_byte & 0x80) != 0);

  // 3. Enter write mode and dirty Track 17 buffer with distinct data
  harness.set_write_mode();
  constexpr uint8_t dirty_byte = 0xAA;
  constexpr size_t write_count = 512;
  for (size_t i = 0; i < write_count; ++i) {
    harness.write_data(dirty_byte);
  }
  CHECK(harness.is_dirty() == true);

  // 4. Step backward across cylinder boundary: Phase 34 (Track 17) -> Phase 33
  // (Track 16). In the unfixed code, disk_ptr->track was mutated to 16 before
  // checking is_dirty, resulting in Track 17's dirty buffer being written to
  // Track 16, clobbering Track 16 and leaving Track 17 unflushed.
  harness.step_backward_phase();

  CHECK(harness.get_phase() == 33);
  CHECK(harness.get_track() == 16);
  CHECK(harness.is_dirty() == false);

  // 5. Verify on-disk file state
  const std::vector<uint8_t> after_track16 =
      read_disk_track_bytes(harness.fixture_path(), 16);
  const std::vector<uint8_t> after_track17 =
      read_disk_track_bytes(harness.fixture_path(), 17);

  // Track 16 must NOT be overwritten or corrupted by Track 17's flush
  CHECK(after_track16 == orig_track16);

  // Track 17 must receive the flushed modifications
  CHECK(after_track17 != orig_track17);

  // 6. Step to full phase of Track 16 (Phase 32) and ensure Track 16 remains
  // pristine
  harness.step_backward_phase();
  CHECK(harness.get_phase() == 32);
  CHECK(harness.get_track() == 16);
  CHECK(harness.is_dirty() == false);

  const std::vector<uint8_t> after_phase32_track16 =
      read_disk_track_bytes(harness.fixture_path(), 16);
  CHECK(after_phase32_track16 == orig_track16);

  // Query peripheral status to ensure drive is healthy
  harness.set_read_mode();
  DiskStatus_t status{};
  size_t status_size = sizeof(status);
  peripheral_query(slot_6, disk_cmd_get_status, &status, &status_size);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == disk_err_none);
}

TEST_CASE("DiskStepper: [STEP-05] Motor Spindown Flushes Dirty Track") {
  DiskStepperHarness_t harness("Master.dsk");

  // Baseline read of Track 0 contents from disk image file
  const std::vector<uint8_t> orig_track0 =
      read_disk_track_bytes(harness.fixture_path(), 0);

  // Head starts at Track 0
  CHECK(harness.get_phase() == 0);
  CHECK(harness.get_track() == 0);
  CHECK(harness.is_dirty() == false);

  // 1. Prime Track 0 into memory
  const uint8_t prime_byte = harness.read_data();
  CHECK((prime_byte & 0x80) != 0);

  // 2. Enter write mode and dirty Track 0
  harness.set_write_mode();
  constexpr uint8_t dirty_byte = 0xAA;
  constexpr size_t write_count = 512;
  for (size_t i = 0; i < write_count; ++i) {
    harness.write_data(dirty_byte);
  }
  CHECK(harness.is_dirty() == true);

  // 3. Power off motor via softswitch $C0E8
  harness.power_motor_off();

  // Motor is now spinning down; track must remain dirty until spindown finishes
  CHECK(harness.get_spinning_ticks() > 0);
  CHECK(harness.is_dirty() == true);

  // 4. Advance emulation cycles to allow motor to complete spindown (20,000
  // ticks * 64 cycles)
  harness.think(motor_spindown_cycles);

  // Spindown has completed; dirty track must be flushed and dirty flag cleared
  CHECK(harness.get_spinning_ticks() == 0);
  CHECK(harness.is_dirty() == false);

  // 5. Verify that the dirty data was flushed to Track 0 on the disk file
  const std::vector<uint8_t> after_spindown_track0 =
      read_disk_track_bytes(harness.fixture_path(), 0);
  CHECK(after_spindown_track0 != orig_track0);

  // 6. Verify peripheral reports drive is no longer spinning and status is
  // clean
  DiskStatus_t status{};
  size_t status_size = sizeof(status);
  peripheral_query(slot_6, disk_cmd_get_status, &status, &status_size);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_spinning == 0);
  CHECK(status.drive0_last_error == disk_err_none);
}
