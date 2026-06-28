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
#include "doctest.h"

// Since Main.cpp is already linked into 'linapple' (headless target),
// we can't easily link it here. We'll implement a test that replicates
// the logic of Main.cpp but with assertions.

TEST_CASE("Headless: [HL-01] Boot from --d1") {
  Linapple_Init();

  Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1,
                                      "../tests/fixtures/minimal.woz");

  Peripheral_Manager_Init(); Linapple_RegisterPeripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus ps =
      Peripheral_Query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == PERIPHERAL_OK);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);

  Linapple_Shutdown();
}

TEST_CASE("Headless: [HL-02] Both drives loaded") {
  Linapple_Init();

  Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1,
                                      "../tests/fixtures/minimal.woz");
  Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE2,
                                      "../tests/fixtures/minimal.dsk");

  Peripheral_Manager_Init(); Linapple_RegisterPeripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus ps =
      Peripheral_Query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == PERIPHERAL_OK);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive1_loaded == true);

  Linapple_Shutdown();
}

TEST_CASE("Headless: [HL-03] Unsupported file") {
  Linapple_Init();

  // .txt is unsupported by disk drivers
  Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1,
                                      "../tests/fixtures/minimal.txt");

  Peripheral_Manager_Init(); Linapple_RegisterPeripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus ps =
      Peripheral_Query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == PERIPHERAL_OK);
  // It shouldn't be loaded, and error should be unsupported format (or file not
  // found if it doesn't exist) Actually our minimal.txt doesn't exist in
  // fixtures yet? I'll check. Assuming it's unsupported if it exists but isn't
  // a disk.
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_unsupported_format);

  Linapple_Shutdown();
}

TEST_CASE("Headless: [HL-04] Program loading") {
  Linapple_Init();
  Peripheral_Manager_Init(); Linapple_RegisterPeripherals();

  int err = Linapple_LoadProgram("../tests/fixtures/minimal.woz");
  CHECK(err != 0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  Peripheral_Query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == false);

  Linapple_Shutdown();
}
