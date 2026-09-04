// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "doctest.h"
#include "test_fixtures.h"

TEST_CASE("DiskIntegration: [INT-05] Runtime Eject Clears Config") {
  linapple_init();
  Configuration_t::instance().set_string(
      "Slots", REGVALUE_DISK_IMAGE1,
      TestFixtures::get_fixture_path("minimal.woz"));
  peripheral_manager_init();
  linapple_register_peripherals();

  DiskEjectCmd_t cmd{};
  cmd.drive = disk_drive_0;

  peripheral_command(6, disk_cmd_eject, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  std::string saved =
      Configuration_t::instance().get_string("Slots", REGVALUE_DISK_IMAGE1);
  CHECK(saved.empty());

  linapple_shutdown();
}
