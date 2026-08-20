#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>

#include "apple2/peripherals/disk/Disk.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "doctest.h"

TEST_CASE("DiskIntegration: [INT-01] Startup Config Loading") {
  linapple_init();
  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                         "../tests/fixtures/minimal.woz");

  peripheral_manager_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == true);

  linapple_shutdown();
}

TEST_CASE("DiskIntegration: [INT-02] Missing Startup Image") {
  linapple_init();
  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                         "nonexistent.dsk");

  peripheral_manager_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_file_not_found);

  linapple_shutdown();
}
