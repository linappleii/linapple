// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/Registry.h"
#include "doctest.h"
#include "frontends/common/HarddiskFrontend.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig = TestFixtures::ScopedTestConfig;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;
constexpr const char* image_key = "Harddisk Image 2";

auto harddisk_in_slot_7() -> TestConfig::Description {
  TestConfig::Description description;
  description.slots[card_slot - 1] = "Harddisk";
  return description;
}

auto status() -> HarddiskStatus {
  HarddiskStatus out{};
  size_t size = sizeof(out);
  REQUIRE(peripheral_query(card_slot, harddisk_query_status, &out, &size) ==
          peripheral_ok);
  return out;
}

}  // namespace

TEST_CASE(
    "Harddisk configuration at a run-time eject: the helper empties the "
    "drive's key and field and saves") {
  TestConfig config(harddisk_in_slot_7());
  TestFixtures::ScopedCore core(config);
  REQUIRE(peripheral_present(card_slot, harddisk_id));
  harddisk_frontend_initialize();
  REQUIRE(harddisk_frontend_slot() == card_slot);
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");

  REQUIRE(harddisk_frontend_insert(1, image.c_str(), false) == 0);
  REQUIRE(status().drive1_loaded == 1);
  REQUIRE(Configuration::instance().get_string("Preferences", image_key) ==
          image.path());

  CHECK(harddisk_frontend_eject(1) == 0);
  CHECK(status().drive1_loaded == 0);
  CHECK(Configuration::instance().get_string("Preferences", image_key).empty());
  CHECK(std::string(Configuration::instance().harddisk_path.at(1).data())
            .empty());

  // Ejecting an empty drive is not an error.
  CHECK(harddisk_frontend_eject(1) == 0);
}

TEST_CASE(
    "Harddisk configuration at a run-time eject: with no hard disk in the "
    "machine the helper says so in the log and does nothing") {
  TestConfig config(TestConfig::enhanced_2e_only());
  TestFixtures::ScopedCore core(config);
  harddisk_frontend_initialize();
  TestFixtures::ScopedLogCapture log;

  CHECK(harddisk_frontend_eject(0) == harddisk_frontend_no_card);
  CHECK(log.count_containing("no hard disk is installed") == 1);
}
