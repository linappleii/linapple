// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <string>

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
constexpr uint16_t io_motor_off = 0xC0E8;
constexpr uint16_t io_motor_on = 0xC0E9;
constexpr uint16_t io_drive_1 = 0xC0EB;
constexpr uint16_t io_q6_clear = 0xC0EC;
constexpr uint16_t io_q6_set = 0xC0ED;
constexpr uint16_t io_q7_clear = 0xC0EE;
constexpr uint16_t io_q7_set = 0xC0EF;

constexpr uint8_t marker = 0x3C;
constexpr uint8_t other_marker = 0xA5;

// One full motor-enable window plus slack, in CPU cycles: the card counts the
// window down in units of 64 cycles.
constexpr uint32_t cycles_to_expire_motor = 1400000;

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

class DiskModesHarness_t {
 public:
  explicit DiskModesHarness_t(bool write_protected) {
    machine_.load();
    linapple_init();
    peripheral_manager_init();
    linapple_register_peripherals();

    disk_ = TestFixtures::create_ephemeral("minimal.dsk");

    DiskInsertCmd_t cmd{};
    cmd.drive = disk_drive_0;
    cmd.write_protected = write_protected ? 1 : 0;
    util_safe_strcpy(cmd.path, disk_.path().c_str(), disk_insert_path_max);
    peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
    peripheral_manager_think(0);
  }

  ~DiskModesHarness_t() { linapple_shutdown(); }

  DiskModesHarness_t(const DiskModesHarness_t&) = delete;
  auto operator=(const DiskModesHarness_t&) -> DiskModesHarness_t& = delete;
  DiskModesHarness_t(DiskModesHarness_t&&) = delete;
  auto operator=(DiskModesHarness_t&&) -> DiskModesHarness_t& = delete;

  auto read_access(uint16_t address) -> uint8_t {
    return io_map_dispatch(0, address, 0, 0, 0);
  }

  auto write_access(uint16_t address, uint8_t value) -> void {
    io_map_dispatch(0, address, 1, value, 0);
  }

  auto think(uint32_t cycles) -> void { peripheral_manager_think(cycles); }

  auto state() const -> DiskSavedState_t {
    DiskSavedState_t saved{};
    size_t size = sizeof(saved);
    peripheral_save_state(slot_6, &saved, &size);
    return saved;
  }

  auto data_register() const -> uint8_t { return state().io_latch; }

  // Reaching a mode is itself a pair of accesses, so the helper leaves the
  // register wherever those accesses put it; the cases that care load it
  // afterwards.
  auto select_mode(bool q7, bool q6) -> void {
    read_access(q7 ? io_q7_set : io_q7_clear);
    read_access(q6 ? io_q6_set : io_q6_clear);
  }

  auto load_register(uint8_t value) -> void {
    read_access(io_q7_set);
    write_access(io_q6_set, value);
  }

 private:
  TestConfig_t machine_{TestConfig_t::disk_ii_only()};
  TestFixtures::EphemeralDiskFixture_t disk_;
};

}  // namespace

TEST_CASE("DiskModes: [MODE-01] Each Q7/Q6 pair picks its own function") {
  for (const bool protect : {false, true}) {
    CAPTURE(protect);
    DiskModesHarness_t harness(protect);
    harness.read_access(io_motor_on);

    // {Q7=0, Q6=0} reads the medium: every nibble the head hands over has
    // bit 7 set, which no other function can produce from a blank register.
    harness.load_register(0x00);
    harness.read_access(io_q7_clear);
    const uint8_t medium_byte = harness.read_access(io_q6_clear);
    CHECK((medium_byte & 0x80U) != 0);
    CHECK(harness.data_register() == medium_byte);

    // {Q7=0, Q6=1} shifts the write-protect line in until the register is
    // all ones or all zeros.
    harness.read_access(io_q6_set);
    CHECK(harness.data_register() == (protect ? 0xFFU : 0x00U));

    // {Q7=1, Q6=1} loads what the 6502 put on the bus.
    harness.load_register(marker);
    CHECK(harness.data_register() == marker);

    // {Q7=1, Q6=0} shifts the register out at the head and leaves it alone.
    harness.read_access(io_q6_clear);
    CHECK(harness.data_register() == marker);
  }
}

TEST_CASE("DiskModes: [MODE-02] Eight write-side cells leave the register") {
  for (const bool protect : {false, true}) {
    CAPTURE(protect);
    DiskModesHarness_t harness(protect);
    harness.read_access(io_motor_on);

    // Cells 1 and 2: a write to $C0xC in shift-write mode puts the register
    // on the medium rather than taking the byte off the bus.
    harness.load_register(marker);
    harness.write_access(io_q6_clear, other_marker);
    CHECK(harness.data_register() == marker);

    // Cells 3 and 4: a write to $C0xE with Q6 already clear selects read,
    // and read needs the read/write strobe it is not.
    harness.load_register(marker);
    harness.read_access(io_q6_clear);
    harness.write_access(io_q7_clear, other_marker);
    CHECK(harness.data_register() == marker);

    // Cells 5 and 6: a write to $C0xF with Q6 clear selects shift-write,
    // which never takes the bus.
    harness.load_register(marker);
    harness.read_access(io_q6_clear);
    harness.write_access(io_q7_set, other_marker);
    CHECK(harness.data_register() == marker);

    // Cells 7 and 8: a write to $C0xF with Q6 set selects load, but the load
    // strobe is $C0xD, so the byte waits there.
    harness.load_register(marker);
    harness.write_access(io_q7_set, other_marker);
    CHECK(harness.data_register() == marker);
  }
}

TEST_CASE("DiskModes: [MODE-03] Write protect answers in either Q7 state") {
  for (const bool protect : {false, true}) {
    for (const bool start_in_write_mode : {false, true}) {
      CAPTURE(protect);
      CAPTURE(start_in_write_mode);
      DiskModesHarness_t harness(protect);
      harness.read_access(io_motor_on);

      harness.load_register(marker);
      harness.read_access(start_in_write_mode ? io_q7_set : io_q7_clear);

      // LDA $C08D,X / LDA $C08E,X: the second read is the answer, and it is
      // saturated rather than a single bit in an otherwise stale byte.
      harness.read_access(io_q6_set);
      const uint8_t sensed = harness.read_access(io_q7_clear);
      CHECK(sensed == (protect ? 0xFFU : 0x00U));
      CHECK(harness.data_register() == sensed);
    }
  }
}

TEST_CASE("DiskModes: [MODE-04] A stopped drive holds the data register") {
  DiskModesHarness_t harness(false);
  harness.read_access(io_motor_on);

  harness.read_access(io_q7_clear);
  const uint8_t held = harness.read_access(io_q6_clear);
  CHECK((held & 0x80U) != 0);

  harness.read_access(io_motor_off);
  harness.think(cycles_to_expire_motor);
  REQUIRE(harness.state().drives[0].spinning_ticks == 0);

  constexpr uint32_t slices = 10;
  constexpr uint32_t cycles_per_slice = 1000;
  for (uint32_t slice = 0; slice < slices; ++slice) {
    harness.read_access(io_q6_clear);
    harness.read_access(io_q6_set);
    harness.think(cycles_per_slice);
  }

  CHECK(harness.data_register() == held);
}

TEST_CASE("DiskModes: [MODE-05] Motor off drops the magnets and keeps Q6") {
  DiskModesHarness_t harness(true);
  harness.read_access(io_motor_on);

  harness.read_access(io_phase_1_on);
  harness.load_register(marker);
  REQUIRE(harness.state().stepper_phase_mask != 0);

  harness.read_access(io_motor_off);

  const DiskSavedState_t after_off = harness.state();
  CHECK(after_off.stepper_phase_mask == 0);
  CHECK(after_off.is_write_mode != 0);
  CHECK(after_off.io_latch == marker);

  // Q6 is not in the v1 state, so it is read back through what it selects:
  // clearing Q7 with Q6 still set senses write protect, which a cleared Q6
  // would have left as a plain read of nothing.
  harness.read_access(io_motor_on);
  harness.read_access(io_q7_clear);
  CHECK(harness.data_register() == 0xFF);
}

TEST_CASE("DiskModes: [MODE-06] Reset clears every switch on the card") {
  DiskModesHarness_t harness(true);
  harness.read_access(io_motor_on);
  harness.read_access(io_drive_1);
  harness.read_access(io_phase_1_on);
  harness.load_register(marker);

  peripheral_manager_reset();

  const DiskSavedState_t after_reset = harness.state();
  CHECK(after_reset.stepper_phase_mask == 0);
  CHECK(after_reset.active_drive_index == 0);
  CHECK(after_reset.is_motor_on == 0);
  CHECK(after_reset.is_write_mode == 0);

  harness.read_access(io_motor_on);
  const uint8_t before_probe = harness.data_register();
  harness.read_access(io_q7_clear);
  CHECK(harness.data_register() == before_probe);
}
