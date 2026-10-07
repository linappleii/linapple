// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;

}  // namespace

TEST_CASE(
    "Harddisk registry: the id resolves and a Slot 7 line places the card") {
  Peripheral_t* descriptor = peripheral_find_internal(harddisk_id);
  REQUIRE(descriptor != nullptr);
  CHECK(std::string(descriptor->id) == harddisk_id);

  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Harddisk";
  TestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);

  CHECK(peripheral_present(card_slot, harddisk_id));
  SS_PERIPHERAL_MANIFEST manifest;
  peripheral_get_manifest(&manifest);
  CHECK(std::string(manifest.peripherals[card_slot].name) == "Harddisk");
}
