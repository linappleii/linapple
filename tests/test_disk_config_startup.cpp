// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "doctest.h"
#include "test_fixtures.h"

namespace {
// Declared rather than inherited: with no configuration the slot fallbacks in
// peripheral_register_internal supply a printer, a Super Serial Card and a
// Mockingboard beside the Disk II, none of which these cases touch.
using TestConfig_t = TestFixtures::ScopedTestConfig_t;
}  // namespace

TEST_CASE("DiskIntegration: [INT-01] Startup Config Loading") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  Configuration_t::instance().set_string(
      "Slots", REGVALUE_DISK_IMAGE1,
      TestFixtures::get_fixture_path("minimal.woz"));

  peripheral_manager_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_query_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == true);

  linapple_shutdown();
}

TEST_CASE("DiskIntegration: [INT-02] Missing Startup Image") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                         "nonexistent.dsk");

  peripheral_manager_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_query_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_file_not_found);

  linapple_shutdown();
}
