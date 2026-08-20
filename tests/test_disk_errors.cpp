#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdio>
#include <cstring>
#include <vector>

#include "apple2/peripherals/disk/Disk.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {
constexpr int SL6 = 6;
}

TEST_CASE("DiskErrors: [ERR-01] Propagate File Not Found") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  Util_SafeStrCpy(cmd.path, "nonexistent_file.dsk", disk_insert_path_max);

  // Command usually returns OK because it's queued, but here internal
  // synchronously executes for local tests.
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(SL6, disk_cmd_get_status, &status, &size);

  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive0_last_error ==
        static_cast<int32_t>(disk_err_file_not_found));

  linapple_shutdown();
}

TEST_CASE("DiskErrors: [ERR-02] Propagate Unsupported Format") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  // Create a garbage file that isn't a valid disk
  const char* garbage = "garbage.txt";
  {
    FILE* f = fopen(garbage, "wb");
    fprintf(f, "This is not a disk image.");
    fclose(f);
  }

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  Util_SafeStrCpy(cmd.path, garbage, disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(SL6, disk_cmd_get_status, &status, &size);

  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive0_last_error ==
        static_cast<int32_t>(disk_err_unsupported_format));

  remove(garbage);
  linapple_shutdown();
}

TEST_CASE("DiskErrors: [ERR-03] Successful insertion clears error") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;

  // First, cause an error
  Util_SafeStrCpy(cmd.path, "missing.dsk", disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  // Now insert valid
  std::string fixture = TestFixtures::get_fixture_path("minimal.dsk");
  Util_SafeStrCpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(SL6, disk_cmd_get_status, &status, &size);

  CHECK(status.drive0_loaded != 0);
  CHECK(status.drive0_last_error == static_cast<int32_t>(disk_err_none));

  linapple_shutdown();
}
