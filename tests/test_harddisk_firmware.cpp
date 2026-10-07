// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstdint>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;
constexpr uint16_t program_start = 0x0300;
constexpr uint16_t copy_at = 0x0400;
constexpr uint32_t cycle_cap = 1000;

auto harddisk_in_slot_7() -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Harddisk";
  return description;
}

}  // namespace

TEST_CASE(
    "Harddisk firmware: the 6502 reads the three ProDOS block-device ID bytes "
    "from the card's page") {
  TestConfig_t config(harddisk_in_slot_7());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(peripheral_present(card_slot, harddisk_id));

  // LDA $C701 / STA $0400, the same for $C703 and $C705, then a spin.
  const std::array<uint8_t, 21> program = {
      0xAD, 0x01, 0xC7, 0x8D, 0x00, 0x04, 0xAD, 0x03, 0xC7, 0x8D, 0x01,
      0x04, 0xAD, 0x05, 0xC7, 0x8D, 0x02, 0x04, 0x4C, 0x12, 0x03};
  const uint16_t sentinel = program_start + 18;
  TestFixtures::ScopedCore_t::poke(program_start, program);
  TestFixtures::enter_at({program_start, 0, 0, 0});
  TestFixtures::step_until_pc(sentinel, cycle_cap);
  REQUIRE(cpu_get_registers()->pc == sentinel);

  // ProDOS 8 Technical Reference Manual, 6.3.1.
  CHECK(mem[copy_at] == 0x20);
  CHECK(mem[copy_at + 1] == 0x00);
  CHECK(mem[copy_at + 2] == 0x03);
}
