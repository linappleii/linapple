// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/Peripheral.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;

}  // namespace

TEST_CASE(
    "Harddisk configuration at start-up: a Slot 7 line gives the machine the "
    "card") {
  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Harddisk";
  TestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_present(card_slot, harddisk_id));
}
