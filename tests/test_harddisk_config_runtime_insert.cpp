// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "doctest.h"
#include "frontends/common/HarddiskFrontend.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestConfig = TestFixtures::ScopedTestConfig;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;
constexpr const char* image_key = "Harddisk Image 1";
constexpr const char* missing_path = "/nonexistent/linapple-missing.hdv";

struct Report {
  int drive;
  int error;
  std::string message;
};

std::vector<Report> reports;

auto record_report(int drive, int error, const char* message) -> void {
  reports.push_back({drive, error, message});
}

// The reporter is a process global, so it is put back when the case ends.
struct ScopedReporter {
  ScopedReporter() {
    reports.clear();
    harddisk_frontend_set_error_reporter(record_report);
  }
  ~ScopedReporter() { harddisk_frontend_set_error_reporter(nullptr); }
  ScopedReporter(const ScopedReporter&) = delete;
  auto operator=(const ScopedReporter&) -> ScopedReporter& = delete;
  ScopedReporter(ScopedReporter&&) = delete;
  auto operator=(ScopedReporter&&) -> ScopedReporter& = delete;
};

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

auto saved_key() -> std::string {
  return Configuration::instance().get_string("Preferences", image_key);
}

auto saved_field() -> std::string {
  return Configuration::instance().harddisk_path.at(0).data();
}

}  // namespace

TEST_CASE(
    "Harddisk configuration at a run-time insert: the helper records the "
    "image the drive holds, and a refused image is recorded as "
    "empty and reported once") {
  TestConfig config(harddisk_in_slot_7());
  TestFixtures::ScopedCore core(config);
  REQUIRE(peripheral_present(card_slot, harddisk_id));
  harddisk_frontend_initialize();
  REQUIRE(harddisk_frontend_slot() == card_slot);
  ScopedReporter reporter;
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");

  CHECK(harddisk_frontend_insert(0, image.c_str(), false) == 0);
  CHECK(status().drive0_loaded == 1);
  CHECK(saved_key() == image.path());
  CHECK(saved_field() == image.path());
  CHECK(reports.empty());

  CHECK(harddisk_frontend_insert(0, missing_path, false) ==
        harddisk_err_not_found);
  CHECK(status().drive0_loaded == 0);
  CHECK(saved_key().empty());
  CHECK(saved_field().empty());
  REQUIRE(reports.size() == 1);
  CHECK(reports.at(0).drive == 0);
  CHECK(reports.at(0).error == harddisk_err_not_found);
  CHECK(reports.at(0).message == "file not found or unreadable");
}

TEST_CASE(
    "Harddisk configuration at a run-time insert: the card itself writes no "
    "key, so an insert sent straight to it leaves the configuration alone") {
  TestConfig config(harddisk_in_slot_7());
  TestFixtures::ScopedCore core(config);
  REQUIRE(peripheral_present(card_slot, harddisk_id));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  REQUIRE(saved_key().empty());

  HarddiskInsertCmd cmd{};
  cmd.drive = harddisk_drive_0;
  std::strncpy(cmd.path, image.c_str(), sizeof(cmd.path) - 1);
  REQUIRE(peripheral_command(card_slot, harddisk_cmd_insert, &cmd,
                             sizeof(cmd)) == peripheral_ok);
  peripheral_manager_think(0);
  CHECK(status().drive0_loaded == 1);
  CHECK(saved_key().empty());
  CHECK(saved_field().empty());
}

TEST_CASE(
    "Harddisk configuration at a run-time insert: with no hard disk in the "
    "machine the helper says so in the log and does nothing") {
  TestConfig config(TestConfig::enhanced_2e_only());
  TestFixtures::ScopedCore core(config);
  harddisk_frontend_initialize();
  CHECK(harddisk_frontend_slot() == harddisk_frontend_no_card);
  TestFixtures::ScopedLogCapture log;
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");

  CHECK(harddisk_frontend_insert(0, image.c_str(), false) ==
        harddisk_frontend_no_card);
  CHECK(log.count_containing("no hard disk is installed") == 1);
  CHECK(saved_key().empty());
}
