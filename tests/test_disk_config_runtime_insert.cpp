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

TEST_CASE("DiskIntegration: [INT-04] Runtime Insert Leaves Config Alone") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  // Initial state: empty
  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1, "");

  std::string fixture = TestFixtures::get_fixture_path("minimal.woz");
  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  strcpy(cmd.path, fixture.c_str());

  peripheral_command(6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  // The card models a drive. Which image the user keeps in it is the
  // frontend's to remember, so a mechanical insert writes nothing.
  std::string saved =
      Configuration_t::instance().get_string("Slots", REGVALUE_DISK_IMAGE1);
  CHECK(saved.empty());

  // The frontend acted on the user's behalf, so the frontend records it.
  app_controller_save_disk_config(0);
  saved = Configuration_t::instance().get_string("Slots", REGVALUE_DISK_IMAGE1);
  CHECK(saved == fixture);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_query_status, &status, &size);
  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);

  // A frontend records the change the moment it issues the command, while the
  // command is still queued, so recording has to reach past the queue.
  const std::string second_fixture =
      TestFixtures::get_fixture_path("minimal.dsk");
  DiskInsertCmd_t second{};
  second.drive = disk_drive_0;
  strcpy(second.path, second_fixture.c_str());
  peripheral_command(6, disk_cmd_insert, &second, sizeof(second));

  app_controller_save_disk_config(0);
  saved = Configuration_t::instance().get_string("Slots", REGVALUE_DISK_IMAGE1);
  CHECK(saved == second_fixture);

  linapple_shutdown();
}
