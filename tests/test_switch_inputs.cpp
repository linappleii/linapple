// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "apple2/Memory.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint8_t bit7 = 0x80;

}  // namespace

TEST_CASE(
    "Switch inputs: $C010 reads bit 7 clear on an Enhanced //e with no key "
    "held") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  CHECK((io_map_dispatch(0, addr_keyboard_strobe, 0, 0, 0) & bit7) == 0);
}
