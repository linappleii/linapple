// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "core/LinAppleCore.h"
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
// Long enough to reach the first address field: gap 1 is 48 sync bytes, and
// only the field behind it says which track the head is on.
constexpr int track_sample_bytes = 64;

// Declared rather than inherited: with no configuration the slot fallbacks in
// peripheral_register_internal supply a printer, a Super Serial Card and a
// Mockingboard beside the Disk II, none of which these cases touch.
using TestConfig_t = TestFixtures::ScopedTestConfig_t;

class DiskStepperHarness_t {
 public:
  explicit DiskStepperHarness_t(const std::string& fixture_name = "Master.dsk")
      : disk_fixture_(TestFixtures::create_ephemeral(fixture_name)) {
    machine_.load();
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
    io_map_dispatch(0, addr, 0, 0, slice_cycle_);
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

  // The sequencer hands a byte over once and then clears it, so a caller
  // after the next byte polls the way RWTS does.
  auto read_data() -> uint8_t {
    constexpr int poll_limit = 40000;
    for (int poll = 0; poll < poll_limit; ++poll) {
      slice_cycle_ += 2;
      const uint8_t value =
          io_map_dispatch(0, read_write_switch, 0, 0, slice_cycle_);
      if (slice_cycle_ >= slice_limit) {
        end_slice();
      }
      if ((value & 0x80U) == 0) {
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

  // One byte out of the head the way RWTS does it: load the register, drop
  // back to shift-write and give the medium the eight cells it needs.
  auto write_data(uint8_t val) -> void {
    io_map_dispatch(0, latch_switch, 1, val, slice_cycle_);
    slice_cycle_ += 4;
    io_map_dispatch(0, read_write_switch, 0, 0, slice_cycle_);
    slice_cycle_ += 28;
    if (slice_cycle_ >= slice_limit) {
      end_slice();
    }
  }

  auto set_read_mode() -> void {
    io_map_dispatch(0, read_mode_switch, 0, 0, slice_cycle_);
  }

  auto power_motor_on() -> void {
    io_map_dispatch(0, motor_on_switch, 0, 0, slice_cycle_);
  }

  auto power_motor_off() -> void {
    end_slice();
    io_map_dispatch(0, motor_off_switch, 0, 0, 0);
  }

  auto think(uint32_t cycles) -> void {
    end_slice();
    peripheral_manager_think(cycles);
  }

  auto set_write_mode() -> void {
    io_map_dispatch(0, write_mode_switch, 0, 0, slice_cycle_);
  }

  auto save_state(DiskSavedState_t& out_state) const -> void {
    size_t size = sizeof(out_state);
    peripheral_save_state(slot_6, &out_state, &size);
  }

  // The synthesised track starts at gap 1, so parking the head at the index
  // hole puts the write over sync bytes no sector needs.
  auto park_at_index_hole() -> void {
    end_slice();
    DiskSavedState_t state{};
    save_state(state);
    state.drives[0].current_byte_pos = 0;
    peripheral_load_state(slot_6, &state, sizeof(state));
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

  static constexpr uint32_t slice_limit = 4096;
  uint32_t slice_cycle_ = 0;
  bool register_is_clear_ = true;

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
    DiskStatus_t status{};
    size_t status_size = sizeof(status);
    peripheral_query(slot_6, disk_query_status, &status, &status_size);
    REQUIRE(status.drive0_loaded == 1);

    io_map_dispatch(0, drive0_select_switch, 0, 0, 0);
    io_map_dispatch(0, motor_on_switch, 0, 0, 0);
    io_map_dispatch(0, read_mode_switch, 0, 0, 0);
    peripheral_manager_think(spin_settle_cycles);
  }

  TestConfig_t machine_{TestConfig_t::disk_ii_only()};
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
  track0_bytes.reserve(track_sample_bytes);
  for (int i = 0; i < track_sample_bytes; ++i) {
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
  track1_bytes.reserve(track_sample_bytes);
  for (int i = 0; i < track_sample_bytes; ++i) {
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
  track2_bytes.reserve(track_sample_bytes);
  for (int i = 0; i < track_sample_bytes; ++i) {
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

TEST_CASE("DiskStepper: [STEP-03] Seeking offers the dirty track") {
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

  harness.park_at_index_hole();
  constexpr uint8_t unique_byte = 0xA5;
  harness.write_data(unique_byte);

  // Track must be marked dirty prior to seek
  CHECK(harness.is_dirty() == true);
  {
    DiskSavedState_t state{};
    harness.save_state(state);
    CHECK(state.drives[0].is_dirty == 1);
  }

  // 3. Leave write mode before seeking: the head writes whatever the shift
  // register holds for as long as it is enabled, and a run of zeros over the
  // gap is a track the image would refuse.
  harness.set_read_mode();
  harness.step_to_track(1);
  CHECK(harness.get_track() == 1);

  // Stepping the head offers the dirty track to the image. The byte landed
  // in gap 1, so all sixteen sectors still read back and the image takes it.
  CHECK(harness.is_dirty() == false);
  {
    DiskSavedState_t state{};
    harness.save_state(state);
    CHECK(state.drives[0].is_dirty == 0);
    CHECK(state.drives[0].track == 1);
  }

  harness.step_to_track(0);
  CHECK(harness.get_track() == 0);

  // Query driver to verify healthy status following flushed write
  DiskStatus_t status{};
  size_t status_size = sizeof(status);
  peripheral_query(slot_6, disk_query_status, &status, &status_size);
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

  // A run of 0xAA is not sixteen readable sectors, so the sector image
  // refuses the whole track and the buffer stays dirty.
  CHECK(harness.is_dirty() == true);

  // 5. Verify on-disk file state
  const std::vector<uint8_t> after_track16 =
      read_disk_track_bytes(harness.fixture_path(), 16);
  const std::vector<uint8_t> after_track17 =
      read_disk_track_bytes(harness.fixture_path(), 17);

  // Track 16 must NOT be overwritten or corrupted by Track 17's flush
  CHECK(after_track16 == orig_track16);

  CHECK(after_track17 == orig_track17);

  // 6. Step to full phase of Track 16 (Phase 32) and ensure Track 16 remains
  // pristine
  harness.step_backward_phase();
  CHECK(harness.get_phase() == 32);
  CHECK(harness.get_track() == 16);

  const std::vector<uint8_t> after_phase32_track16 =
      read_disk_track_bytes(harness.fixture_path(), 16);
  CHECK(after_phase32_track16 == orig_track16);

  // Query peripheral status to ensure drive is healthy
  harness.set_read_mode();
  DiskStatus_t status{};
  size_t status_size = sizeof(status);
  peripheral_query(slot_6, disk_query_status, &status, &status_size);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == disk_err_none);
}

TEST_CASE("DiskStepper: [STEP-05] Motor spindown offers the dirty track") {
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

  // 4. Run past the 556 motor-off hold of 1,159,235 cycles, with slack, so
  // the spindown flush has definitely fired before the state is inspected
  harness.think(motor_spindown_cycles);

  // Spindown has completed and the flush was attempted. A run of 0xAA is
  // not sixteen readable sectors, so the sector image refuses the track and
  // the buffer stays dirty.
  CHECK(harness.get_spinning_ticks() == 0);
  CHECK(harness.is_dirty() == true);

  const std::vector<uint8_t> after_spindown_track0 =
      read_disk_track_bytes(harness.fixture_path(), 0);
  CHECK(after_spindown_track0 == orig_track0);

  // 6. Verify peripheral reports drive is no longer spinning and status is
  // clean
  DiskStatus_t status{};
  size_t status_size = sizeof(status);
  peripheral_query(slot_6, disk_query_status, &status, &status_size);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_spinning == 0);
  CHECK(status.drive0_last_error == disk_err_none);
}

TEST_CASE("DiskStepper: [STEP-06] A dead motor timer leaves the head alone") {
  DiskStepperHarness_t harness;

  harness.step_phase(1, true);
  harness.step_phase(1, false);
  harness.step_phase(2, true);
  harness.step_phase(2, false);
  const int32_t moved_track = harness.get_phase();
  CHECK(moved_track > 0);

  harness.power_motor_off();
  harness.think(motor_spindown_cycles);
  REQUIRE(harness.get_spinning_ticks() == 0);

  // The magnets are powered from the same enable the spindle is, so a full
  // step sequence with the timer run out energises nothing.
  harness.step_phase(3, true);
  harness.step_phase(3, false);
  harness.step_phase(0, true);
  harness.step_phase(0, false);
  CHECK(harness.get_phase() == moved_track);
}

namespace {

// A medium the card can read and write that reports which quarter track the
// head asked for, which is the only place that number is visible: the v1
// save state rounds it to the half track below.
constexpr uint32_t recorder_cell_count = 4096;
constexpr uint32_t quarter_track_unread = 0xFFFFFFFFU;
constexpr uint32_t magnet_hold_cycles = 64;

struct TrackRecorder_t {
  std::vector<uint8_t> cells = std::vector<uint8_t>(recorder_cell_count / 8, 0);
  uint32_t last_read_quarter_track = quarter_track_unread;
  uint32_t last_written_quarter_track = quarter_track_unread;
  std::vector<uint8_t> last_written_cells;
};

TrackRecorder_t g_recorder;

auto recorder_probe(const uint8_t*, size_t, uint32_t, const char*)
    -> DiskProbe_e {
  return disk_probe_definite;
}

auto recorder_open(const char*, uint32_t, bool, void** out_instance)
    -> DiskError_e {
  *out_instance = &g_recorder;
  return disk_err_none;
}

auto recorder_close(void*) -> void {}

auto recorder_is_write_protected(void*) -> bool { return false; }

auto recorder_read(void*, uint32_t quarter_track, uint8_t* bits,
                   uint32_t max_bits, uint32_t* out_bit_count,
                   uint8_t* out_bit_timing) -> DiskError_e {
  if (recorder_cell_count > max_bits) {
    return disk_err_unsupported;
  }
  g_recorder.last_read_quarter_track = quarter_track;
  for (size_t byte = 0; byte < g_recorder.cells.size(); ++byte) {
    bits[byte] = g_recorder.cells[byte];
  }
  *out_bit_count = recorder_cell_count;
  *out_bit_timing = disk_default_bit_timing;
  return disk_err_none;
}

auto recorder_write(void*, uint32_t quarter_track, const uint8_t* bits,
                    uint32_t bit_count) -> DiskError_e {
  if (bit_count != recorder_cell_count) {
    return disk_err_unsupported;
  }
  g_recorder.last_written_quarter_track = quarter_track;
  g_recorder.last_written_cells.assign(bits, bits + (bit_count / 8));
  return disk_err_none;
}

auto recorder_driver() -> const DiskFormatDriver_t* {
  static const DiskFormatDriver_t driver = {disk_format_abi_version,
                                            disk_driver_cap_write,
                                            "AAA Quarter Track Recorder",
                                            nullptr,
                                            recorder_probe,
                                            recorder_open,
                                            recorder_close,
                                            recorder_is_write_protected,
                                            recorder_read,
                                            recorder_write,
                                            nullptr};
  return &driver;
}

class QuarterTrackHarness_t {
 public:
  QuarterTrackHarness_t() {
    g_recorder = TrackRecorder_t{};
    machine_.load();
    linapple_init();
    peripheral_manager_init();
    linapple_register_peripherals();
    disk_loader_register(recorder_driver());

    DiskInsertCmd_t cmd{};
    cmd.drive = disk_drive_0;
    util_safe_strcpy(cmd.path, fixture_.c_str(), disk_insert_path_max);
    peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
    peripheral_manager_think(0);

    io_map_dispatch(0, drive0_select_switch, 0, 0, 0);
    io_map_dispatch(0, motor_on_switch, 0, 0, 0);
    io_map_dispatch(0, read_mode_switch, 0, 0, 0);
    io_map_dispatch(0, read_write_switch, 0, 0, 0);
    peripheral_manager_think(magnet_hold_cycles);
    REQUIRE(g_recorder.last_read_quarter_track == 0);
    quarter_track_ = 0;
  }

  ~QuarterTrackHarness_t() {
    linapple_shutdown();
    disk_loader_reset();
  }

  QuarterTrackHarness_t(const QuarterTrackHarness_t&) = delete;
  auto operator=(const QuarterTrackHarness_t&)
      -> QuarterTrackHarness_t& = delete;

  auto strobe(int phase, bool on, uint32_t hold_cycles) -> void {
    const auto address =
        static_cast<uint16_t>(stepper_base + (phase * 2) + (on ? 1 : 0));
    io_map_dispatch(0, address, 0, 0, 0);
    peripheral_manager_think(hold_cycles);
  }

  auto strobe(int phase, bool on) -> void {
    strobe(phase, on, magnet_hold_cycles);
  }

  // Pulling the next track in is what tells the driver where the head is, so
  // one read after a move is how the number gets out.
  auto quarter_track() -> uint32_t {
    io_map_dispatch(0, read_write_switch, 0, 0, 1);
    peripheral_manager_think(2);
    if (g_recorder.last_read_quarter_track != quarter_track_unread) {
      quarter_track_ = g_recorder.last_read_quarter_track;
    }
    return quarter_track_;
  }

  auto write_nibble(uint8_t value, uint32_t* cycle) -> void {
    io_map_dispatch(0, latch_switch, 1, value, *cycle);
    *cycle += 4;
    io_map_dispatch(0, read_write_switch, 0, 0, *cycle);
    *cycle += 28;
  }

  auto set_write_mode(uint32_t cycle) -> void {
    io_map_dispatch(0, write_mode_switch, 0, 0, cycle);
  }

  auto set_read_mode(uint32_t cycle) -> void {
    io_map_dispatch(0, read_mode_switch, 0, 0, cycle);
  }

  auto close_slice(uint32_t cycle) -> void { peripheral_manager_think(cycle); }

 private:
  TestConfig_t machine_{TestConfig_t::disk_ii_only()};
  TestFixtures::EphemeralDiskFixture_t fixture_ =
      TestFixtures::create_ephemeral("minimal.dsk");
  uint32_t quarter_track_ = 0;
};

}  // namespace

TEST_CASE("DiskStepper: [STEP-07] Two magnets park the head between them") {
  QuarterTrackHarness_t harness;

  // Walking the magnets a pair at a time visits the odd quarter tracks the
  // half-track model could never reach.
  std::vector<uint32_t> visited;
  int held = 0;
  for (int step = 0; step < 8; ++step) {
    const int next = (held + 1) & 3;
    harness.strobe(held, true);
    harness.strobe(next, true);
    visited.push_back(harness.quarter_track());
    harness.strobe(held, false);
    held = next;
  }

  for (size_t index = 0; index < visited.size(); ++index) {
    CHECK(visited[index] == (index * 2) + 1);
  }
}

TEST_CASE("DiskStepper: [STEP-08] Recalibration lands on quarter track 0") {
  for (const int start_half_track : {3, 10, 39, 40, 61, 79}) {
    CAPTURE(start_half_track);
    QuarterTrackHarness_t harness;

    for (int step = 0; step < start_half_track; ++step) {
      harness.strobe((step + 1) & 3, true);
      harness.strobe(step & 3, false);
    }
    REQUIRE(harness.quarter_track() > 0);
    for (int phase = 0; phase < 4; ++phase) {
      harness.strobe(phase, false);
    }

    // The P5 boot ROM's recalibration: eighty-one passes with the phase
    // counting down, which drags the head outward past the stop.
    constexpr int recalibration_passes = 81;
    for (int pass = 0; pass < recalibration_passes; ++pass) {
      const int phase = (0x50 - pass) & 3;
      harness.strobe(phase, true);
      harness.strobe(phase, false);
    }
    harness.strobe(0, true);

    CHECK(harness.quarter_track() == 0);
  }
}

TEST_CASE("DiskStepper: [STEP-09] Two magnets dropped together cancel") {
  {
    QuarterTrackHarness_t harness;
    harness.strobe(0, true);
    harness.strobe(1, true);
    const uint32_t parked = harness.quarter_track();
    REQUIRE(parked == 1);

    // Both coils lose current inside the ten cycles the cog needs, so the
    // head never chases the magnet that outlived the other.
    io_map_dispatch(0, stepper_base + 0, 0, 0, 0);
    io_map_dispatch(0, stepper_base + 2, 0, 0, 0);
    peripheral_manager_think(magnet_hold_cycles);
    CHECK(harness.quarter_track() == parked);
  }
  {
    QuarterTrackHarness_t harness;
    harness.strobe(0, true);
    harness.strobe(1, true);
    REQUIRE(harness.quarter_track() == 1);

    // Far enough apart and the head does follow the surviving magnet.
    harness.strobe(0, false);
    harness.strobe(1, false);
    CHECK(harness.quarter_track() == 2);
  }
}

TEST_CASE(
    "DiskStepper: [STEP-10] A seek hands the written track to the driver") {
  QuarterTrackHarness_t harness;

  const std::vector<uint8_t> payload = {0xFF, 0xFF, 0xFF, 0xD5,
                                        0xAA, 0x96, 0xFF, 0xFE};
  uint32_t cycle = 1;
  harness.set_write_mode(cycle);
  for (const uint8_t nibble : payload) {
    harness.write_nibble(nibble, &cycle);
  }
  harness.set_read_mode(cycle);
  harness.close_slice(cycle);

  // Stepping the head is what offers the buffer to the image.
  harness.strobe(1, true);
  harness.strobe(0, false);

  REQUIRE(g_recorder.last_written_quarter_track != quarter_track_unread);
  CHECK(g_recorder.last_written_quarter_track == 0);
  REQUIRE(g_recorder.last_written_cells.size() == recorder_cell_count / 8);

  // The cells the driver was handed decode to the nibbles that went out.
  std::vector<uint8_t> decoded;
  uint32_t assembled = 0;
  int filled = 0;
  for (uint32_t cell = 0; cell < recorder_cell_count && decoded.size() < 16;
       ++cell) {
    const uint32_t bit =
        (g_recorder.last_written_cells[cell >> 3U] >> (7U - (cell & 7U))) & 1U;
    if (filled == 0 && bit == 0) {
      continue;
    }
    assembled = ((assembled << 1U) | bit) & 0xFFU;
    if (++filled == 8) {
      decoded.push_back(static_cast<uint8_t>(assembled));
      assembled = 0;
      filled = 0;
    }
  }

  bool found = false;
  for (size_t index = 0; index + 3 <= decoded.size(); ++index) {
    if (decoded[index] == 0xD5 && decoded[index + 1] == 0xAA &&
        decoded[index + 2] == 0x96) {
      found = true;
      break;
    }
  }
  CHECK(found);
}
