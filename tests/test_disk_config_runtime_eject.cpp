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

TEST_CASE("DiskIntegration: [INT-05] Runtime Eject Leaves Config Alone") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  const std::string fixture = TestFixtures::get_fixture_path("minimal.woz");
  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                         fixture);
  peripheral_manager_init();
  linapple_register_peripherals();

  DiskEjectCmd_t cmd{};
  cmd.drive = disk_drive_0;

  peripheral_command(6, disk_cmd_eject, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  // Taking the disk out of the drive is not by itself the user asking to stop
  // mounting it at startup, so the card leaves the key where it found it.
  std::string saved =
      Configuration_t::instance().get_string("Slots", REGVALUE_DISK_IMAGE1);
  CHECK(saved == fixture);

  // The frontend the user ejected from is what clears the key.
  app_controller_save_disk_config(0);
  saved = Configuration_t::instance().get_string("Slots", REGVALUE_DISK_IMAGE1);
  CHECK(saved.empty());

  linapple_shutdown();
}
