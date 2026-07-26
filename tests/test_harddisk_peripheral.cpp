#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>
#include <vector>

#include "apple2/peripherals/harddisk/Harddisk.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Peripheral.h"
#include "doctest.h"

extern "C" auto harddisk_get_descriptor() -> Peripheral_t*;

TEST_CASE("Harddisk Peripheral: Lifecycle and Registration") {
  linapple_init();
  peripheral_manager_init();

  // Register Harddisk in Slot 7
  int result = peripheral_register(harddisk_get_descriptor(), 7);
  CHECK(result == 0);

  // Verify I/O mapping for $C0F0
  // We expect some value from the harddisk IO handler.
  // By default, without an image, it should return DEVICE_OK (0x00) for most
  // reads.
  uint8_t val = io_map_dispatch(0, 0xC0F2, 0, 0, 0);  // Read Command register
  CHECK(val == 0);

  linapple_shutdown();
}

TEST_CASE("Harddisk Peripheral: Commands and Queries") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register(harddisk_get_descriptor(), 7);

  // 1. Check initial status
  HarddiskStatus_t status;
  size_t size = sizeof(status);
  PeripheralStatus_t pstatus =
      peripheral_query(7, harddisk_cmd_get_status, &status, &size);
  CHECK(pstatus == peripheral_ok);
  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive1_loaded == 0);

  // 2. Test INSERT command (with a non-existent file to check error reporting)
  HarddiskInsertCmd_t insert;
  memset(&insert, 0, sizeof(insert));
  insert.drive = 0;
  strncpy(insert.path, "non_existent_image.hdv", sizeof(insert.path) - 1);

  pstatus = peripheral_command(7, harddisk_cmd_insert, &insert, sizeof(insert));
  CHECK(pstatus == peripheral_ok);

  // Commands are processed during Think
  peripheral_manager_think(0);

  // 3. Verify error in status
  size = sizeof(status);
  pstatus = peripheral_query(7, harddisk_cmd_get_status, &status, &size);
  CHECK(pstatus == peripheral_ok);
  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive0_last_error != 0);  // harddisk_err_not_found

  // 4. Test EJECT command
  HarddiskEjectCmd_t eject;
  eject.drive = 0;
  pstatus = peripheral_command(7, harddisk_cmd_eject, &eject, sizeof(eject));
  CHECK(pstatus == peripheral_ok);
  peripheral_manager_think(0);

  size = sizeof(status);
  pstatus = peripheral_query(7, harddisk_cmd_get_status, &status, &size);
  CHECK(status.drive0_last_error == 0);  // Reset after eject/cleanup

  linapple_shutdown();
}

TEST_CASE("Harddisk Peripheral: Direct I/O Logic") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register(harddisk_get_descriptor(), 7);

  // Write Unit Number to $C0F3
  io_map_dispatch(0, 0xC0F3, 1, 0x80, 0);  // Drive 2

  // Read it back
  uint8_t val = io_map_dispatch(0, 0xC0F3, 0, 0, 0);
  CHECK(val == 0x80);

  // Write Command to $C0F2
  io_map_dispatch(0, 0xC0F2, 1, 0x01, 0);  // Read command
  val = io_map_dispatch(0, 0xC0F2, 0, 0, 0);
  CHECK(val == 0x01);

  linapple_shutdown();
}
