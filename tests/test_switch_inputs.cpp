// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "HeadlessHarness.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SwitchInputs.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/JoystickConfig.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig = TestFixtures::ScopedTestConfig;

constexpr uint16_t addr_keyboard_data = 0xC000;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint16_t addr_cassette_in = 0xC060;
constexpr uint16_t addr_switch0 = 0xC061;
constexpr uint16_t addr_switch2 = 0xC063;
constexpr uint16_t addr_mirror_switch0 = 0xC069;
constexpr uint8_t bit7 = 0x80;
constexpr uint8_t shift_line = 2;
constexpr uint8_t every_line_pulled_down = 0x07;

// One NTSC frame of scanner positions, every byte the undriven bus can hold.
constexpr uint32_t scanner_positions = 17030;
constexpr uint16_t text_page = 0x0400;
// During horizontal blanking a II fetches with A12 set, so its undriven bus
// also shows $1400-$17FF (Sather, Understanding the Apple II, 5-12, 5-13).
constexpr uint16_t text_page_hbl_mirror = 0x1400;
constexpr uint16_t text_page_size = 0x0400;
constexpr uint8_t normal_space = 0xA0;

// A byte with bit 7 set proves the level, not the bus, decides bit 7.
constexpr uint8_t marker_low = 0x5A;
constexpr uint8_t marker_high = 0xDA;

// The Enhanced //e reset reads $C062 and, with bit 7 set, jumps to the
// self-test at $C600 (AD 62 C0 10 03 4C 00 C6 at $C2BB; IIe Tech Ref
// pp. 93-95); with both Apple keys up it reaches the power-up check at $C2E2.
constexpr uint16_t rom_self_test = 0xC600;
constexpr uint16_t rom_cold_start_check = 0xC2E2;
// The Monitor's reset routine is done inside a tenth of a second.
constexpr uint32_t reset_routine_cycle_cap = 100000;

// The text page and the II's blanking page are filled with normal spaces so
// the undriven bus carries bit 7 at every scanner position, and a default
// that fell through to the bus would show in the sweep.
struct SwitchMachine {
  TestConfig config;
  HeadlessHarness harness;

  explicit SwitchMachine(const TestConfig::Description& description)
      : config(description), harness(config) {
    harness.boot();
    const std::array<uint8_t, text_page_size> spaces =
        [] -> std::array<uint8_t, text_page_size> {
      std::array<uint8_t, text_page_size> page{};
      page.fill(normal_space);
      return page;
    }();
    TestFixtures::ScopedCore::poke(text_page, spaces);
    TestFixtures::ScopedCore::poke(text_page_hbl_mirror, spaces);
  }

  // A known byte at position 0's scanner address, so bits 0-6 are known.
  static auto place_bus_marker(uint8_t marker) -> void {
    TestFixtures::ScopedCore::poke(video_get_scanner_address(nullptr, 0),
                                     &marker, 1);
  }
};

auto describe(TestConfig::MachineType model,
              const char* joystick0 = nullptr) -> TestConfig::Description {
  TestConfig::Description description = TestConfig::enhanced_2e_only();
  description.machine_type = model;
  if (joystick0 != nullptr) {
    description.extras.push_back({"Configuration", "Joystick 0", joystick0});
  }
  return description;
}

// A think drains the command queue, as a running machine does once a frame.
auto settle() -> void { peripheral_manager_think(0); }

auto read_at(uint16_t addr, uint32_t position) -> uint8_t {
  return io_map_dispatch(0, addr, 0, 0, position);
}

auto high_at(uint16_t addr, uint32_t position) -> bool {
  return (read_at(addr, position) & bit7) != 0;
}

auto line_level(uint8_t line) -> int {
  return high_at(static_cast<uint16_t>(addr_switch0 + line), 0) ? 1 : 0;
}

auto mirror_level(uint8_t line) -> int {
  return high_at(static_cast<uint16_t>(addr_mirror_switch0 + line), 0) ? 1 : 0;
}

auto samples_with_bit7(uint16_t addr, bool set) -> uint32_t {
  uint32_t count = 0;
  for (uint32_t position = 0; position < scanner_positions; ++position) {
    if (high_at(addr, position) == set) {
      ++count;
    }
  }
  return count;
}

auto hold_apple_keys(bool open_apple, bool solid_apple) -> void {
  linapple_set_modifiers(false, false, open_apple, solid_apple);
  settle();
}

auto hold_shift(bool shift) -> void {
  linapple_set_modifiers(shift, false, false, false);
  settle();
}

const std::initializer_list<TestConfig::MachineType> both_models = {
    TestConfig::machine_apple2e_enhanced,
    TestConfig::machine_apple2_plus,
};

}  // namespace

TEST_CASE(
    "Switch inputs: $C010 reads bit 7 clear on an Enhanced //e with no key "
    "held") {
  TestConfig config(TestConfig::enhanced_2e_only());
  TestFixtures::ScopedCore core(config);
  CHECK((io_map_dispatch(0, addr_keyboard_strobe, 0, 0, 0) & bit7) == 0);
}

TEST_CASE(
    "Switch inputs: no phantom key: with the text page full of spaces $C000 "
    "reads bit 7 clear at every scanner position on both models, and so "
    "does $C010 on the //e") {
  for (TestConfig::MachineType model : both_models) {
    CAPTURE(model);
    SwitchMachine machine(describe(model));
    // The cassette input is the undriven bus on every model, so it shows the
    // sweep can tell a default from the bus.
    REQUIRE(samples_with_bit7(addr_cassette_in, true) == scanner_positions);
    CHECK(samples_with_bit7(addr_keyboard_data, false) == scanner_positions);
    // On a II or II Plus $C010 is the undriven bus (Sather, Understanding
    // the Apple II, 5-25), so only the //e's any-key-down flag reads 0.
    if (model == TestConfig::machine_apple2e_enhanced) {
      CHECK(samples_with_bit7(addr_keyboard_strobe, false) ==
            scanner_positions);
    }
    // The shipped two-button plug holds PB0 and PB1 down and leaves PB2 open
    // at every position: the level, not the bus, decides bit 7.
    CHECK(samples_with_bit7(addr_switch0, false) == scanner_positions);
    CHECK(samples_with_bit7(static_cast<uint16_t>(addr_switch0 + 1), false) ==
          scanner_positions);
    CHECK(samples_with_bit7(addr_switch2, true) == scanner_positions);
  }
}

#ifndef ENABLE_PERIPHERAL_KEYBOARD
TEST_CASE(
    "Switch inputs: keyboard unplugged, inferred from the open 74LS257 "
    "inputs: a II Plus reads $C000 as $7F, $C010-$C01F as the undriven bus, "
    "and its reset routine reaches $FA7E") {
  SwitchMachine machine(describe(TestConfig::machine_apple2_plus));
  CHECK(read_at(addr_keyboard_data, 0) == 0x7F);
  // Any access to $C01X resets the strobe flip-flop and nothing drives the
  // bus on the read, the flags of $C011-$C01F being the //e's (Sather,
  // Understanding the Apple II, 7-4, 7-5, 5-25).
  for (uint16_t addr = addr_keyboard_strobe; addr <= addr_keyboard_strobe + 15;
       ++addr) {
    for (uint32_t position = 0; position < scanner_positions; ++position) {
      if (read_at(addr, position) != read_at(addr_cassette_in, position)) {
        CAPTURE(addr);
        CAPTURE(position);
        FAIL("the read differs from the undriven bus");
      }
    }
  }

  // The Autostart ROM's reset reads no switch input before BIT $C010 at
  // $FA7E (Apple II Reference Manual 1979, Monitor listing).
  constexpr uint16_t reset_clears_strobe = 0xFA7E;
  machine.harness.boot();
  TestFixtures::step_until_pc(reset_clears_strobe, reset_routine_cycle_cap);
  CHECK(cpu_get_registers()->pc == reset_clears_strobe);
}
#endif

TEST_CASE(
    "Switch inputs: with the shipped two-button controller PB0 and PB1 rest "
    "low and PB2 high on both models, at $C061-$C063 and their mirrors") {
  for (TestConfig::MachineType model : both_models) {
    CAPTURE(model);
    SwitchMachine machine(describe(model, "2"));
    CHECK(line_level(0) == 0);
    CHECK(line_level(1) == 0);
    CHECK(line_level(2) == 1);
    CHECK(mirror_level(0) == 0);
    CHECK(mirror_level(1) == 0);
    CHECK(mirror_level(2) == 1);
  }
}

TEST_CASE(
    "Switch inputs: with no controller configured a II Plus reads its three "
    "buttons open, since nothing on its board or keyboard pulls them down") {
  SwitchMachine machine(describe(TestConfig::machine_apple2_plus, "0"));
  for (uint8_t line = 0; line < switch_input_count; ++line) {
    CAPTURE(line);
    CHECK(line_level(line) == 1);
    CHECK(mirror_level(line) == 1);
  }
}

#ifdef ENABLE_PERIPHERAL_KEYBOARD
TEST_CASE(
    "Switch inputs: with no controller configured a //e with its keyboard "
    "reads PB0 and PB1 low through the keyboard's 470 ohm resistors") {
  SwitchMachine machine(
      describe(TestConfig::machine_apple2e_enhanced, "0"));
  CHECK(line_level(0) == 0);
  CHECK(line_level(1) == 0);
  CHECK(line_level(2) == 1);
}
#else
TEST_CASE(
    "Switch inputs: with no controller configured a //e with its keyboard "
    "unplugged reads PB0 and PB1 high through the board's 12 k pull-ups") {
  SwitchMachine machine(
      describe(TestConfig::machine_apple2e_enhanced, "0"));
  CHECK(line_level(0) == 1);
  CHECK(line_level(1) == 1);
  CHECK(line_level(2) == 1);
}
#endif

TEST_CASE(
    "Switch inputs: a second stick configured but not found: the configured "
    "plug pulls PB2 down until the frontend reports the devices it opened, "
    "and PB2 is open afterwards") {
  TestConfig::Description description =
      describe(TestConfig::machine_apple2e_enhanced);
  description.extras.push_back({"Configuration", "Joystick 1", "1"});
  SwitchMachine machine(description);
  // The configured baseline: joystick 1's button sits on PB2 and PB1, so its
  // plug's resistors pull both down beside joystick 0's PB0.
  CHECK(line_level(0) == 0);
  CHECK(line_level(1) == 0);
  CHECK(line_level(2) == 0);

  // The frontend found the keypad for joystick 0 and nothing for joystick 1
  // and reports that plug alone, as the SDL frontends do.
  linapple_set_game_pulldowns(joystick_config_pulldown_mask(2, 0));
  CHECK(line_level(0) == 0);
  CHECK(line_level(1) == 0);
  CHECK(line_level(2) == 1);
}

TEST_CASE(
    "Switch inputs: the mask a terminal sends with /dev/input/js0 open, PB0 "
    "and PB1 beside the configured controller's lines, pulls both down on a II "
    "Plus with no controller configured") {
  SwitchMachine machine(describe(TestConfig::machine_apple2_plus, "0"));
  REQUIRE(line_level(0) == 1);
  REQUIRE(line_level(1) == 1);
  REQUIRE(line_level(2) == 1);
  // A two-button device on the kernel's joystick interface is a second plug
  // with a 560 ohm pull-down on each of its lines (Sather, Understanding the
  // Apple II, 7-9 and 7-11).
  linapple_set_game_pulldowns(joystick_config_pulldown_mask() |
                              joystick_line_pb0 | joystick_line_pb1);
  CHECK(line_level(0) == 0);
  CHECK(line_level(1) == 0);
  CHECK(line_level(2) == 1);
  linapple_set_game_pulldowns(joystick_config_pulldown_mask() |
                              joystick_line_pb0 | joystick_line_pb1 |
                              joystick_line_pb2);
  CHECK(line_level(2) == 0);
}

TEST_CASE(
    "Switch inputs: an override replaces the hardware's pull-downs and -1 "
    "restores them") {
  SwitchMachine machine(describe(TestConfig::machine_apple2e_enhanced));
  REQUIRE(line_level(0) == 0);
  REQUIRE(line_level(2) == 1);
  switch_inputs_override_pulldowns(0);
  CHECK(line_level(0) == 1);
  CHECK(line_level(1) == 1);
  CHECK(line_level(2) == 1);
  switch_inputs_override_pulldowns(every_line_pulled_down);
  CHECK(line_level(0) == 0);
  CHECK(line_level(1) == 0);
  CHECK(line_level(2) == 0);
  switch_inputs_override_pulldowns(-1);
  CHECK(line_level(0) == 0);
  CHECK(line_level(1) == 0);
  CHECK(line_level(2) == 1);
}

#ifndef ENABLE_PERIPHERAL_JOYSTICK
TEST_CASE(
    "Switch inputs: with no game port card every paddle reads high at every "
    "scanner position and PREAD returns 255") {
  SwitchMachine machine(describe(TestConfig::machine_apple2e_enhanced));
  constexpr uint16_t addr_paddle0 = 0xC064;
  constexpr uint16_t addr_mirror_paddle0 = 0xC06C;
  for (uint8_t paddle = 0; paddle < 4; ++paddle) {
    CAPTURE(paddle);
    CHECK(samples_with_bit7(static_cast<uint16_t>(addr_paddle0 + paddle),
                            true) == scanner_positions);
    CHECK(samples_with_bit7(static_cast<uint16_t>(addr_mirror_paddle0 + paddle),
                            true) == scanner_positions);
  }

  // LDX #0; JSR $FB1E; NOP: PREAD counts in Y while bit 7 of $C064,X is set
  // and wraps to 255 on an input that never falls (Apple II Reference Manual
  // 1979 p. 99; the routine's INY / BNE / DEY at $FB1E).
  constexpr uint16_t program_base = 0x0300;
  constexpr uint16_t rom_pread = 0xFB1E;
  constexpr uint32_t pread_cycle_cap = 4000;
  const std::array<uint8_t, 6> caller = {0xA2, 0x00, 0x20, 0x1E, 0xFB, 0xEA};
  constexpr uint16_t sentinel = program_base + 5;
  TestFixtures::ScopedCore::poke(program_base, caller);
  TestFixtures::enter_at({program_base, 0, 0, 0});
  TestFixtures::step_until_pc(rom_pread, pread_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == rom_pread);
  TestFixtures::step_until_pc(sentinel, pread_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == sentinel);
  CHECK(cpu_get_registers()->y == 255);
}
#endif

TEST_CASE(
    "Switch inputs: Open Apple and Solid Apple from the host read on PB0 and "
    "PB1 on both models and survive a hard reset") {
  for (TestConfig::MachineType model : both_models) {
    CAPTURE(model);
    SwitchMachine machine(describe(model, "2"));
    REQUIRE(line_level(0) == 0);
    REQUIRE(line_level(1) == 0);

    hold_apple_keys(true, false);
    CHECK(line_level(0) == 1);
    CHECK(line_level(1) == 0);
    CHECK(mirror_level(0) == 1);

    hold_apple_keys(true, true);
    CHECK(line_level(0) == 1);
    CHECK(line_level(1) == 1);

    // RESET' reaches no switch, and the //e Monitor reads both Apple keys
    // inside its reset routine (IIe Technical Reference pp. 93-95).
    linapple_reset_hard();
    CHECK(line_level(0) == 1);
    CHECK(line_level(1) == 1);

    // The switch is a contact: a reset with the keys up reads them up.
    hold_apple_keys(false, false);
    linapple_reset_hard();
    CHECK(line_level(0) == 0);
    CHECK(line_level(1) == 0);
  }
}

TEST_CASE(
    "Switch inputs: a line reads the OR of its connector button and its Apple "
    "key over the pull-down, with bits 0-6 the undriven bus, at $C061 and "
    "$C069") {
  SwitchMachine machine(describe(TestConfig::machine_apple2e_enhanced));

  // The //e wires Open Apple and Solid Apple in parallel with PB0 and PB1
  // (IIe Tech Ref pp. 13 and 41), so either switch, or both, drives the line
  // high and the line falls only when both are open.
  for (uint8_t line = 0; line < 2; ++line) {
    CAPTURE(line);
    const uint16_t addr = static_cast<uint16_t>(addr_switch0 + line);
    const uint16_t mirror = static_cast<uint16_t>(addr_mirror_switch0 + line);
    REQUIRE(line_level(line) == 0);
    linapple_set_game_switch(line, true);
    CHECK(high_at(addr, 0));
    CHECK(high_at(mirror, 0));
    hold_apple_keys(line == 0, line == 1);
    CHECK(high_at(addr, 0));
    linapple_set_game_switch(line, false);
    CHECK(high_at(addr, 0));
    CHECK(high_at(mirror, 0));
    hold_apple_keys(false, false);
    CHECK_FALSE(high_at(addr, 0));
    CHECK_FALSE(high_at(mirror, 0));
  }

  // Pressed $DA, released $5A, with either scanner byte (IIe Tech Ref p. 41:
  // bits 0-6 are whatever the bus holds).
  for (uint8_t marker : {marker_low, marker_high}) {
    CAPTURE(marker);
    SwitchMachine::place_bus_marker(marker);
    linapple_set_game_switch(0, true);
    CHECK(read_at(addr_switch0, 0) == marker_high);
    CHECK(read_at(addr_mirror_switch0, 0) == marker_high);
    linapple_set_game_switch(0, false);
    CHECK(read_at(addr_switch0, 0) == marker_low);
    CHECK(read_at(addr_mirror_switch0, 0) == marker_low);
  }

  // A line past PB2 is no line: the bridge ignores it.
  linapple_set_game_switch(switch_input_count, true);
  CHECK(line_level(0) == 0);
  CHECK(line_level(1) == 0);
  CHECK(line_level(2) == 1);
}

TEST_CASE(
    "Switch inputs: PB2 follows its button only over a pull-down, and the "
    "shift key only through the jumper, which then overrides the button") {
  SwitchMachine machine(describe(TestConfig::machine_apple2e_enhanced));

  // Jumper out: PB2 is open with a two-button plug, so 1 at rest and 1 with
  // the button; a three-button plug's pull-down makes the button visible; the
  // shift key is not wired to the line, so shift down leaves it as it was.
  REQUIRE(line_level(shift_line) == 1);
  linapple_set_game_switch(shift_line, true);
  CHECK(line_level(shift_line) == 1);
  linapple_set_game_switch(shift_line, false);
  hold_shift(true);
  CHECK(line_level(shift_line) == 1);
  hold_shift(false);

  linapple_set_game_pulldowns(every_line_pulled_down);
  CHECK(line_level(shift_line) == 0);
  linapple_set_game_switch(shift_line, true);
  CHECK(line_level(shift_line) == 1);
  linapple_set_game_switch(shift_line, false);
  hold_shift(true);
  CHECK(line_level(shift_line) == 0);
  hold_shift(false);
  // A mask wider than the three lines is cut to them.
  linapple_set_game_pulldowns(0xFF);
  CHECK(line_level(shift_line) == 0);
  linapple_set_game_pulldowns(0x03);

  // Jumper in: the single-wire shift-key mod grounds the line through the
  // shift key (IIe Tech Ref p. 41; Sather IIe 7-31), so shift down reads 0
  // and shift up 1 whatever the connector button and the mask do.
  linapple_set_shift_key_mod(true);
  CHECK(line_level(shift_line) == 1);
  hold_shift(true);
  CHECK(line_level(shift_line) == 0);
  linapple_set_game_switch(shift_line, true);
  CHECK(line_level(shift_line) == 0);
  hold_shift(false);
  CHECK(line_level(shift_line) == 1);
  linapple_set_game_switch(shift_line, false);
  linapple_set_game_pulldowns(every_line_pulled_down);
  CHECK(line_level(shift_line) == 1);
  hold_shift(true);
  CHECK(line_level(shift_line) == 0);
  hold_shift(false);
  CHECK(line_level(shift_line) == 1);
  linapple_set_game_pulldowns(0x03);
  linapple_set_shift_key_mod(false);
  CHECK(line_level(shift_line) == 1);
}

TEST_CASE(
    "Switch inputs: the jumper and the pull-down mask survive a hard reset "
    "and a loaded game-port frame") {
  SwitchMachine machine(describe(TestConfig::machine_apple2e_enhanced));

  // Soldered, not state: neither a reset nor a frame carries the mask or the
  // jumper away.
  linapple_set_game_pulldowns(every_line_pulled_down);
  linapple_set_shift_key_mod(true);
  hold_shift(true);
  CHECK_FALSE(high_at(addr_switch2, 0));
  hold_shift(false);
  CHECK(high_at(addr_switch2, 0));

  linapple_reset_hard();
  hold_shift(true);
  CHECK_FALSE(high_at(addr_switch2, 0));
  hold_shift(false);
  CHECK(high_at(addr_switch2, 0));

#ifdef ENABLE_PERIPHERAL_JOYSTICK
  std::array<uint8_t, 56> frame{};
  size_t size = frame.size();
  peripheral_save_state_by_name(0, "Joystick", frame.data(), &size);
  REQUIRE(size == frame.size());
  peripheral_load_state_by_name(0, "Joystick", frame.data(), frame.size());
  hold_shift(true);
  CHECK_FALSE(high_at(addr_switch2, 0));
  hold_shift(false);
  CHECK(high_at(addr_switch2, 0));
#endif

  // With the jumper out again the mask is what shows: PB2 pulled down rests
  // at 0 and reads the button.
  linapple_set_shift_key_mod(false);
  CHECK_FALSE(high_at(addr_switch2, 0));
  linapple_set_game_switch(shift_line, true);
  CHECK(high_at(addr_switch2, 0));
  linapple_set_game_switch(shift_line, false);
  CHECK_FALSE(high_at(addr_switch2, 0));
}

TEST_CASE(
    "Switch inputs: the bridge records the host's four modifier levels, Open "
    "Apple and Solid Apple reach PB0 and PB1 on both models, and letting go "
    "of every key releases them") {
  for (TestConfig::MachineType model : both_models) {
    CAPTURE(model);
    SwitchMachine machine(describe(model, "2"));
    bool shift = true;
    bool ctrl = true;
    bool open_apple = true;
    bool solid_apple = true;
    linapple_get_modifiers(&shift, &ctrl, &open_apple, &solid_apple);
    CHECK_FALSE(shift);
    CHECK_FALSE(ctrl);
    CHECK_FALSE(open_apple);
    CHECK_FALSE(solid_apple);
    REQUIRE(line_level(0) == 0);
    REQUIRE(line_level(1) == 0);

    linapple_set_modifiers(true, false, true, false);
    settle();
    linapple_get_modifiers(&shift, &ctrl, &open_apple, &solid_apple);
    CHECK(shift);
    CHECK_FALSE(ctrl);
    CHECK(open_apple);
    CHECK_FALSE(solid_apple);
    CHECK(line_level(0) == 1);
    CHECK(line_level(1) == 0);

    linapple_set_modifiers(false, true, false, true);
    settle();
    linapple_get_modifiers(&shift, &ctrl, &open_apple, &solid_apple);
    CHECK_FALSE(shift);
    CHECK(ctrl);
    CHECK_FALSE(open_apple);
    CHECK(solid_apple);
    CHECK(line_level(0) == 0);
    CHECK(line_level(1) == 1);

    // A null out-pointer asks for nothing.
    linapple_get_modifiers(nullptr, nullptr, nullptr, nullptr);

    // Focus loss lets go of the Apple keys with the matrix keys.
    linapple_set_modifiers(true, true, true, true);
    settle();
    REQUIRE(line_level(0) == 1);
    linapple_set_key_release_all();
    settle();
    linapple_get_modifiers(&shift, &ctrl, &open_apple, &solid_apple);
    CHECK_FALSE(shift);
    CHECK_FALSE(ctrl);
    CHECK_FALSE(open_apple);
    CHECK_FALSE(solid_apple);
    CHECK(line_level(0) == 0);
    CHECK(line_level(1) == 0);
  }
}

TEST_CASE(
    "Switch inputs: the Enhanced //e ROM run from its reset vector takes the "
    "self-test at $C600 with Solid Apple held and the cold-start check at "
    "$C2E2 with both Apple keys up") {
  SwitchMachine machine(describe(TestConfig::machine_apple2e_enhanced));

  // Without the key the reset routine passes the check at $C2BB and reaches
  // the power-up byte check; with it, the JMP at $C2C0 is taken.
  linapple_reset_hard();
  TestFixtures::step_until_pc(rom_cold_start_check, reset_routine_cycle_cap);
  CHECK(cpu_get_registers()->pc == rom_cold_start_check);

  hold_apple_keys(false, true);
  linapple_reset_hard();
  const uint32_t cycles =
      TestFixtures::step_until_pc(rom_self_test, reset_routine_cycle_cap);
  CHECK(cpu_get_registers()->pc == rom_self_test);
  CHECK(cycles < reset_routine_cycle_cap);

  hold_apple_keys(false, false);
  linapple_reset_hard();
  TestFixtures::step_until_pc(rom_cold_start_check, reset_routine_cycle_cap);
  CHECK(cpu_get_registers()->pc == rom_cold_start_check);
}

TEST_CASE(
    "Switch inputs: a //e told its keyboard is unplugged, with no plug, reads "
    "PB1 high through the board's pull-ups and its reset takes the self-test "
    "on every one of sixteen reset phases") {
  SwitchMachine machine(
      describe(TestConfig::machine_apple2e_enhanced, "0"));
  constexpr uint16_t rom_read_solid_apple = 0xC2BB;
  constexpr uint32_t phase_step_cycles = 1931;
  constexpr int phase_count = 16;

  // The bridge's record for a //e with no keyboard card: every mask and switch
  // cleared and the 470 ohm resistors gone, so the revision C board's 12 k
  // pull-ups alone set the level (Technical Note #9).
  switch_inputs_reset_configuration(true, false);
  REQUIRE(line_level(0) == 1);
  REQUIRE(line_level(1) == 1);
  REQUIRE(line_level(2) == 1);

  for (int trial = 0; trial < phase_count; ++trial) {
    CAPTURE(trial);
    machine.harness.boot();
    uint32_t spent = 0;
    while (spent < static_cast<uint32_t>(trial) * phase_step_cycles) {
      spent += cpu_execute(0);
    }
    machine.harness.reset_soft();
    TestFixtures::step_until_pc(rom_read_solid_apple, reset_routine_cycle_cap);
    REQUIRE(cpu_get_registers()->pc == rom_read_solid_apple);
    cpu_execute(0);
    cpu_execute(0);
    CHECK(cpu_get_registers()->pc == rom_read_solid_apple + 5);
    cpu_execute(0);
    CHECK(cpu_get_registers()->pc == rom_self_test);
  }
}
