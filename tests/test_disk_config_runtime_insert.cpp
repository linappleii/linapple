#include "Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "doctest.h"
#include "test_fixtures.h"

TEST_CASE("DiskIntegration: [INT-04] Runtime Insert Updates Config") {
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

  std::string saved =
      Configuration_t::instance().get_string("Slots", REGVALUE_DISK_IMAGE1);
  CHECK(saved == fixture);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);
  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);

  linapple_shutdown();
}
