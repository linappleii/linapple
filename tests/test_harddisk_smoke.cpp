// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstddef>
#include <string>

#include "HeadlessHarness.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;

auto harddisk_in_slot_7() -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Harddisk";
  return description;
}

auto status() -> HarddiskStatus_t {
  HarddiskStatus_t out{};
  size_t size = sizeof(out);
  REQUIRE(peripheral_query(card_slot, harddisk_query_status, &out, &size) ==
          peripheral_ok);
  return out;
}

}  // namespace

TEST_CASE("Harddisk smoke: the card is still in its slot after a boot") {
  TestConfig_t config(harddisk_in_slot_7());
  HeadlessHarness_t harness(config);
  harness.boot();
  harness.run_frames(2);

  CHECK(peripheral_present(card_slot, harddisk_id));
}

TEST_CASE(
    "Harddisk smoke: an image named on the command line is in drive 1 when "
    "the machine starts") {
  TestConfig_t config(harddisk_in_slot_7());
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");
  std::string program = "linapple";
  std::string flag = "--hd1";
  std::string path = image.path();
  std::array<char*, 3> argv = {&program[0], &flag[0], &path[0]};
  HeadlessHarness_t harness(config, static_cast<int>(argv.size()), argv.data());
  peripheral_manager_think(0);

  REQUIRE(peripheral_present(card_slot, harddisk_id));
  const HarddiskStatus_t loaded = status();
  CHECK(loaded.drive0_loaded == 1);
  CHECK(std::string(loaded.drive0_full_path) == image.path());
}
