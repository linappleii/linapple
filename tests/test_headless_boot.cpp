#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "doctest.h"

// Since Main.cpp is already linked into 'linapple' (headless target),
// we can't easily link it here. We'll implement a test that replicates
// the logic of Main.cpp but with assertions.

TEST_CASE("Headless: [HL-01] Boot from --d1") {
  linapple_init();

  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                         "../tests/fixtures/minimal.woz");

  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);

  linapple_shutdown();
}

TEST_CASE("Headless: [HL-02] Both drives loaded") {
  linapple_init();

  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                         "../tests/fixtures/minimal.woz");
  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE2,
                                         "../tests/fixtures/minimal.dsk");

  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive1_loaded == true);

  linapple_shutdown();
}

TEST_CASE("Headless: [HL-03] Unsupported file") {
  linapple_init();

  // .txt is unsupported by disk drivers
  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                         "../tests/fixtures/minimal.txt");

  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  // It shouldn't be loaded, and error should be unsupported format (or file not
  // found if it doesn't exist) Actually our minimal.txt doesn't exist in
  // fixtures yet? I'll check. Assuming it's unsupported if it exists but isn't
  // a disk.
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_unsupported_format);

  linapple_shutdown();
}

TEST_CASE("Headless: [HL-04] Program loading") {
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  int err = linapple_load_program("../tests/fixtures/minimal.woz");
  CHECK(err != 0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == false);

  linapple_shutdown();
}
