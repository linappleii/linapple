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
constexpr uint16_t io_motor_off = 0xC0E8;
constexpr uint16_t io_motor_on = 0xC0E9;
constexpr uint16_t io_drive_1 = 0xC0EB;
constexpr uint16_t io_q6_clear = 0xC0EC;
constexpr uint16_t io_q6_set = 0xC0ED;
constexpr uint16_t io_q7_clear = 0xC0EE;
constexpr uint16_t io_q7_set = 0xC0EF;

constexpr uint8_t marker = 0x3C;
constexpr uint8_t other_marker = 0xA5;

// Eight cells of medium at the default timing, long enough for any of the
// four sequencer functions to reach the data register.
constexpr uint32_t settle_cycles = 32;

// One full motor-off hold of 1,159,235 cycles plus slack.
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

  auto read_at(uint16_t address, uint32_t cycle) -> uint8_t {
    return io_map_dispatch(0, address, 0, 0, cycle);
  }

  auto write_at(uint16_t address, uint32_t cycle, uint8_t value) -> void {
    io_map_dispatch(0, address, 1, value, cycle);
  }

  // Each case runs inside one slice, so an access carries the cycle it
  // happens on and the slice is closed when the medium has to move on
  // without the 6502 touching the card.
  auto read(uint16_t address) -> uint8_t { return read_at(address, cycle_); }
  auto write(uint16_t address, uint8_t value) -> void {
    write_at(address, cycle_, value);
  }
  auto advance(uint32_t cycles) -> void { cycle_ += cycles; }
  auto close_slice() -> void {
    peripheral_manager_think(cycle_);
    cycle_ = 0;
  }
  auto cycle() const -> uint32_t { return cycle_; }

  auto think(uint32_t cycles) -> void {
    close_slice();
    peripheral_manager_think(cycles);
  }

  auto state() const -> DiskSavedState_t {
    DiskSavedState_t saved{};
    size_t size = sizeof(saved);
    peripheral_save_state(slot_6, &saved, &size);
    return saved;
  }

  auto data_register() const -> uint8_t { return state().io_latch; }

  auto start_drive() -> void { read(io_motor_on); }

  auto select_mode(bool q7, bool q6) -> void {
    read(q7 ? io_q7_set : io_q7_clear);
    read(q6 ? io_q6_set : io_q6_clear);
  }

  auto load_register(uint8_t value) -> void {
    read(io_q7_set);
    write(io_q6_set, value);
    advance(settle_cycles);
    read(io_q6_set);
  }

 private:
  TestConfig_t machine_{TestConfig_t::disk_ii_only()};
  TestFixtures::EphemeralDiskFixture_t disk_;
  uint32_t cycle_ = 0;
};

}  // namespace

TEST_CASE("DiskModes: [MODE-01] Each Q7/Q6 pair picks its own function") {
  for (const bool protect : {false, true}) {
    CAPTURE(protect);
    DiskModesHarness_t harness(protect);
    harness.start_drive();

    // {Q7=1, Q6=1} loads what the 6502 put on the bus.
    harness.load_register(marker);
    CHECK(harness.data_register() == marker);

    // {Q7=0, Q6=1} shifts the write-protect line in until the register is
    // all ones or all zeros, never a partial byte.
    harness.select_mode(false, true);
    harness.advance(settle_cycles);
    harness.read(io_q6_set);
    CHECK(harness.data_register() == (protect ? 0xFFU : 0x00U));

    // {Q7=0, Q6=0} reads the medium: a whole byte arrives with bit 7 set,
    // which neither of the other read-side functions can produce.
    harness.select_mode(false, false);
    bool saw_a_nibble = false;
    for (int poll = 0; poll < 2000 && !saw_a_nibble; ++poll) {
      harness.advance(2);
      saw_a_nibble = (harness.read(io_q6_clear) & 0x80U) != 0;
    }
    CHECK(saw_a_nibble);

    // {Q7=1, Q6=0} shifts the register out at the head, so the byte the 6502
    // loaded is gone from the register a byte-time later and the medium
    // carries it instead.
    harness.load_register(marker);
    harness.select_mode(true, false);
    harness.advance(settle_cycles);
    harness.read(io_q6_clear);
    CHECK(harness.data_register() == 0x00);
    CHECK(harness.state().drives[0].is_dirty == (protect ? 0 : 1));
    harness.close_slice();
  }
}

TEST_CASE("DiskModes: [MODE-02] No mode switch moves the register itself") {
  // The sequencer owns the data register; the switches only choose which
  // function runs. These are the eight write-side cells - the four mode
  // switches taken as 6502 writes, with the drive write-protected and not -
  // and in none of them does the access change what the register holds.
  // $C0xD is the only one carrying a byte at all, and even there the load
  // waits for the sequencer.
  for (const bool protect : {false, true}) {
    for (const uint16_t address :
         {io_q6_clear, io_q6_set, io_q7_clear, io_q7_set}) {
      CAPTURE(protect);
      CAPTURE(address);
      DiskModesHarness_t harness(protect);
      harness.start_drive();
      harness.load_register(marker);

      // Reading first brings the sequencer up to this cycle, so the write
      // that follows on the same cycle steps nothing.
      const uint32_t cycle = harness.cycle();
      harness.read_at(address, cycle);
      const uint8_t before = harness.data_register();
      harness.write_at(address, cycle, other_marker);
      CHECK(harness.data_register() == before);
    }
  }
}

TEST_CASE("DiskModes: [MODE-03] Write protect answers in either Q7 state") {
  for (const bool protect : {false, true}) {
    for (const bool start_in_write_mode : {false, true}) {
      CAPTURE(protect);
      CAPTURE(start_in_write_mode);
      DiskModesHarness_t harness(protect);
      harness.start_drive();

      harness.load_register(marker);
      harness.read(start_in_write_mode ? io_q7_set : io_q7_clear);

      // LDA $C08D,X / LDA $C08E,X, then the instruction time the 6502 spends
      // before it can look: the answer is saturated rather than one bit in an
      // otherwise stale byte.
      harness.read(io_q6_set);
      harness.advance(4);
      harness.read(io_q7_clear);
      harness.advance(settle_cycles);
      harness.close_slice();
      CHECK(harness.data_register() == (protect ? 0xFFU : 0x00U));
    }
  }
}

TEST_CASE("DiskModes: [MODE-04] A stopped drive holds the data register") {
  DiskModesHarness_t harness(false);
  harness.start_drive();
  harness.load_register(marker);
  const uint8_t held = harness.data_register();
  CHECK(held == marker);

  harness.read(io_motor_off);
  harness.think(cycles_to_expire_motor);
  REQUIRE(harness.state().drives[0].spinning_ticks == 0);

  constexpr uint32_t slices = 10;
  constexpr uint32_t cycles_per_slice = 1000;
  for (uint32_t slice = 0; slice < slices; ++slice) {
    harness.read(io_q6_clear);
    harness.read(io_q6_set);
    harness.think(cycles_per_slice);
  }

  CHECK(harness.data_register() == held);
}

TEST_CASE("DiskModes: [MODE-05] Motor off drops the magnets and keeps Q6") {
  DiskModesHarness_t harness(false);
  harness.start_drive();

  harness.read(io_phase_1_on);
  harness.load_register(marker);
  REQUIRE(harness.state().stepper_phase_mask != 0);

  harness.read(io_motor_off);

  const DiskSavedState_t after_off = harness.state();
  CHECK(after_off.stepper_phase_mask == 0);
  CHECK(after_off.is_write_mode != 0);
  CHECK(after_off.io_latch == marker);

  // Q6 is not in the v1 state, so it is read back through what it selects:
  // with Q6 still set, clearing Q7 senses write protect and saturates the
  // register to zero, where a cleared Q6 would have read a nibble instead.
  harness.read(io_motor_on);
  harness.read(io_q7_clear);
  harness.advance(settle_cycles);
  harness.read(io_q6_set);
  CHECK(harness.data_register() == 0x00);
}

TEST_CASE("DiskModes: [MODE-06] Reset clears every switch on the card") {
  DiskModesHarness_t harness(false);
  harness.start_drive();
  harness.read(io_drive_1);
  harness.read(io_phase_1_on);
  harness.load_register(marker);
  harness.close_slice();

  peripheral_manager_reset();

  const DiskSavedState_t after_reset = harness.state();
  CHECK(after_reset.stepper_phase_mask == 0);
  CHECK(after_reset.active_drive_index == 0);
  CHECK(after_reset.is_motor_on == 0);
  CHECK(after_reset.is_write_mode == 0);

  // With both mode bits clear the sequencer reads the medium, so a nibble
  // turns up; a Q6 that had survived the reset would have saturated the
  // register to zero instead.
  harness.start_drive();
  bool saw_a_nibble = false;
  for (int poll = 0; poll < 2000 && !saw_a_nibble; ++poll) {
    harness.advance(2);
    saw_a_nibble = (harness.read(io_q6_clear) & 0x80U) != 0;
  }
  CHECK(saw_a_nibble);
}
