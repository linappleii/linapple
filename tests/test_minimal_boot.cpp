// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "HeadlessHarness.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr uint32_t prompt_frame_cap = 300;
constexpr uint32_t prompt_again_frame_cap = 120;
constexpr uint32_t idle_frames = 120;
constexpr int text_rows = 24;
constexpr int disk_slot = 6;

constexpr uint16_t addr_keyboard_data = 0xC000;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint16_t addr_cassette_in = 0xC060;
constexpr uint16_t addr_switch0 = 0xC061;
constexpr uint16_t addr_switch1 = 0xC062;
constexpr uint16_t addr_switch2 = 0xC063;
constexpr uint16_t addr_paddle0 = 0xC064;
constexpr uint8_t bit7 = 0x80;
constexpr uint16_t addr_ch = 0x0024;
constexpr uint16_t addr_cv = 0x0025;

// One NTSC frame of scanner positions, every byte the undriven bus can hold.
constexpr uint32_t scanner_positions = 17030;

// The Enhanced //e reset reads $C062 at $C2BB and branches at $C2BE to $C2C3
// with PB1 low, else JMP $C600 into the self-test (AD 62 C0 10 03 4C 00 C6;
// IIe Technical Reference pp. 93-95); with PB0 low too $C2C6 falls to $C2E2.
constexpr uint16_t rom_read_solid_apple = 0xC2BB;
constexpr uint16_t rom_read_open_apple = 0xC2C3;
constexpr uint16_t rom_past_apple_keys = 0xC2E2;
constexpr uint32_t reset_routine_cycle_cap = 100000;

// The II's Monitor draws the cursor as a flashing space, $60 (Apple II
// Reference Manual 1979 p. 15); the //e's cursor byte the harness trims. The
// self-test's RAM patterns can put a "]" in column 0, so no prefix would do.
auto is_prompt_row(const std::string& text) -> bool {
  constexpr char flashing_space = 0x60;
  return text == "]" || text == std::string("]") + flashing_space;
}

auto has_prompt_row(const HeadlessHarness_t& harness) -> bool {
  for (int row = 0; row < text_rows; ++row) {
    if (is_prompt_row(harness.get_text_row(row))) {
      return true;
    }
  }
  return false;
}

auto prompt_row_within(HeadlessHarness_t& harness, uint32_t cap) -> bool {
  for (uint32_t frame = 0; frame < cap; ++frame) {
    if (has_prompt_row(harness)) {
      return true;
    }
    harness.run_frames(1);
  }
  return has_prompt_row(harness);
}

auto screen(const HeadlessHarness_t& harness) -> std::vector<std::string> {
  std::vector<std::string> rows;
  rows.reserve(text_rows);
  for (int row = 0; row < text_rows; ++row) {
    rows.push_back(harness.get_text_row(row, false));
  }
  return rows;
}

auto drive_spinning() -> bool {
  DiskStatus_t status{};
  size_t size = sizeof(status);
  REQUIRE(peripheral_query(disk_slot, disk_query_status, &status, &size) ==
          peripheral_ok);
  return status.drive0_spinning != 0;
}

struct Boot_t {
  TestConfig_t config;
  HeadlessHarness_t harness;
  TestFixtures::EphemeralDiskFixture_t disk;

  explicit Boot_t(const TestConfig_t::Description_t& description)
      : config(description),
        harness(config),
        disk(TestFixtures::create_ephemeral("Master.dsk")) {
    harness.mount_disk(disk_slot, 0, disk);
  }
};

auto describe(TestConfig_t::MachineType_t model,
              const char* joystick0 = nullptr) -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description = TestConfig_t::disk_ii_only();
  description.machine_type = model;
  if (joystick0 != nullptr) {
    description.extras.push_back({"Configuration", "Joystick 0", joystick0});
  }
  return description;
}

// A card's bus bridge takes the cycle count the frame left, so each position
// is read as that count plus the position: every scanner position once, never
// a count below the bridge's last.
struct Sweep_t {
  uint32_t base;

  explicit Sweep_t(uint32_t frame_cycles) : base(frame_cycles) {}

  auto read(uint16_t addr, uint32_t position) const -> uint8_t {
    return io_map_dispatch(0, addr, 0, 0, base + position);
  }

  auto samples_with_bit7(uint16_t addr, bool set) const -> uint32_t {
    uint32_t count = 0;
    for (uint32_t position = 0; position < scanner_positions; ++position) {
      if (((read(addr, position) & bit7) != 0) == set) {
        ++count;
      }
    }
    return count;
  }
};

size_t g_video_frames = 0;

auto count_video_frame(const uint32_t* pixels, int width, int height, int pitch)
    -> void {
  (void)pixels;
  (void)width;
  (void)height;
  (void)pitch;
  ++g_video_frames;
}

const std::initializer_list<TestConfig_t::MachineType_t> both_models = {
    TestConfig_t::machine_apple2e_enhanced, TestConfig_t::machine_apple2_plus};

}  // namespace

TEST_CASE(
    "Minimal boot: DOS 3.3 on a Disk II with the speaker boots to the prompt "
    "on both models, idles there with the screen, the cursor and the drive "
    "still, no phantom key at any scanner position, no sound and one picture "
    "a frame, and comes back to the prompt after a soft reset") {
  for (TestConfig_t::MachineType_t model : both_models) {
    CAPTURE(model);
    const bool apple2e = model == TestConfig_t::machine_apple2e_enhanced;
    Boot_t boot(describe(model));
    HeadlessHarness_t& harness = boot.harness;
    harness.boot();
    REQUIRE(prompt_row_within(harness, prompt_frame_cap));
    while (drive_spinning()) {
      harness.run_frames(1);
    }
    harness.run_frames(10);

    // Idle at the prompt the cone is at rest, so no sample is delivered.
    const std::vector<std::string> prompt_screen = screen(harness);
    const uint8_t ch = *mem_get_main_ptr(addr_ch);
    const uint8_t cv = *mem_get_main_ptr(addr_cv);
    const size_t audio_before = harness.get_audio_sample_count();
    video_set_rendering_enabled(true);
    g_video_frames = 0;
    linapple_set_video_callback(count_video_frame);
    harness.run_frames(idle_frames - 1);
    const uint32_t last_frame_cycles = linapple_run_frame(scanner_positions);
    linapple_set_video_callback(nullptr);
    video_set_rendering_enabled(false);
    CHECK(screen(harness) == prompt_screen);
    CHECK(*mem_get_main_ptr(addr_ch) == ch);
    CHECK(*mem_get_main_ptr(addr_cv) == cv);
    CHECK_FALSE(drive_spinning());
    CHECK(harness.get_audio_sample_count() == audio_before);
    CHECK(g_video_frames == idle_frames);

    // No phantom key: the //e's any-key-down is down, the II Plus's $C010 is
    // the undriven bus the cassette input also shows, PB0 and PB1 rest low and
    // PB2 high through the shipped two-button plug, and with no game port card
    // every paddle reads high.
    REQUIRE(last_frame_cycles >= scanner_positions);
    const Sweep_t sweep(last_frame_cycles);
    CHECK(sweep.samples_with_bit7(addr_keyboard_data, false) ==
          scanner_positions);
    if (apple2e) {
      CHECK(sweep.samples_with_bit7(addr_keyboard_strobe, false) ==
            scanner_positions);
    } else {
      for (uint32_t position = 0; position < scanner_positions; ++position) {
        if (sweep.read(addr_keyboard_strobe, position) !=
            sweep.read(addr_cassette_in, position)) {
          CAPTURE(position);
          FAIL("$C010 differs from the undriven bus");
        }
      }
    }
    CHECK(sweep.samples_with_bit7(addr_switch0, false) == scanner_positions);
    CHECK(sweep.samples_with_bit7(addr_switch1, false) == scanner_positions);
    CHECK(sweep.samples_with_bit7(addr_switch2, true) == scanner_positions);
    if (!peripheral_present(0, "linapple.joystick")) {
      CHECK(sweep.samples_with_bit7(addr_paddle0, true) == scanner_positions);
    }

    // The //e reset reads PB1 low at $C2BB and skips the self-test.
    harness.reset_soft();
    if (apple2e) {
      TestFixtures::step_until_pc(rom_read_solid_apple,
                                  reset_routine_cycle_cap);
      REQUIRE(cpu_get_registers()->pc == rom_read_solid_apple);
      cpu_execute(0);
      cpu_execute(0);
      CHECK(cpu_get_registers()->pc == rom_read_open_apple);
      cpu_execute(0);
      cpu_execute(0);
      CHECK(cpu_get_registers()->pc == rom_past_apple_keys);
    }
    harness.run_frames(1);
    CHECK(prompt_row_within(harness, prompt_again_frame_cap));
  }
}

#if !defined(ENABLE_PERIPHERAL_KEYBOARD)

namespace {

constexpr uint16_t rom_self_test_jump = 0xC2C0;
constexpr uint16_t rom_self_test = 0xC600;
// The self-test's loop test, LDA $C061 / AND $C062 / ASL / INC $FF / LDA $FF /
// BCC $C7A4 / JMP $C600 (bytes AD 61 C0 2D 62 C0 0A E6 FF A5 FF 90 03 4C 00 C6
// at $C794): with both lines high the burn-in starts the test again.
constexpr uint16_t rom_loop_test = 0xC794;
constexpr uint16_t rom_loop_test_restart = 0xC7A1;

// One pass of the self-test, $C600 to its loop test, was 33,558,555 cycles
// when first measured, about half a second of host time.
constexpr uint32_t self_test_pass_cap = 40000000;

constexpr uint32_t phase_step_cycles = 1931;
constexpr int phase_count = 16;

// The program counter after the Apple-key branch says which way it went.
auto reset_to_apple_key_branch(HeadlessHarness_t& harness) -> uint16_t {
  harness.reset_soft();
  TestFixtures::step_until_pc(rom_read_solid_apple, reset_routine_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == rom_read_solid_apple);
  cpu_execute(0);
  cpu_execute(0);
  return cpu_get_registers()->pc;
}

auto run_phase(HeadlessHarness_t& harness, int trial) -> void {
  harness.boot();
  uint32_t spent = 0;
  while (spent < static_cast<uint32_t>(trial) * phase_step_cycles) {
    spent += cpu_execute(0);
  }
}

}  // namespace

TEST_CASE(
    "Minimal boot: a //e with no keyboard card and no controller configured "
    "reads PB1 high at reset, enters the self-test, and its burn-in loop test "
    "re-enters it, on every one of sixteen reset phases") {
  Boot_t boot(describe(TestConfig_t::machine_apple2e_enhanced, "0"));
  HeadlessHarness_t& harness = boot.harness;

  harness.boot();
  REQUIRE(reset_to_apple_key_branch(harness) == rom_self_test_jump);
  cpu_execute(0);
  REQUIRE(cpu_get_registers()->pc == rom_self_test);

  // The loop test reads both lines high and restarts the test.
  TestFixtures::step_until_pc(rom_loop_test, self_test_pass_cap);
  REQUIRE(cpu_get_registers()->pc == rom_loop_test);
  for (int instruction = 0; instruction < 6; ++instruction) {
    cpu_execute(0);
  }
  CHECK(cpu_get_registers()->pc == rom_loop_test_restart);
  cpu_execute(0);
  CHECK(cpu_get_registers()->pc == rom_self_test);

  for (int trial = 0; trial < phase_count; ++trial) {
    CAPTURE(trial);
    run_phase(harness, trial);
    CHECK(reset_to_apple_key_branch(harness) == rom_self_test_jump);
  }
}

TEST_CASE(
    "Minimal boot: a //e with no keyboard card and the shipped two-button "
    "controller reads PB1 low at reset, passes the Apple-key checks and boots "
    "DOS 3.3 to the prompt, on every one of sixteen reset phases") {
  Boot_t boot(describe(TestConfig_t::machine_apple2e_enhanced, "2"));
  HeadlessHarness_t& harness = boot.harness;

  harness.boot();
  REQUIRE(reset_to_apple_key_branch(harness) == rom_read_open_apple);
  cpu_execute(0);
  cpu_execute(0);
  CHECK(cpu_get_registers()->pc == rom_past_apple_keys);
  harness.run_frames(1);
  CHECK(prompt_row_within(harness, prompt_frame_cap));

  for (int trial = 0; trial < phase_count; ++trial) {
    CAPTURE(trial);
    run_phase(harness, trial);
    CHECK(reset_to_apple_key_branch(harness) == rom_read_open_apple);
  }
}

#endif
