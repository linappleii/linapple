// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include <cstdint>
#include <string>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdio>
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "core/LinAppleCore.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {
// Declared rather than inherited: with no configuration the slot fallbacks in
// peripheral_register_internal supply a printer, a Super Serial Card, a
// Mockingboard and a hard disk that nothing here touches.
using TestConfig_t = TestFixtures::ScopedTestConfig_t;
}  // namespace

namespace {
constexpr int SL6 = 6;
}

TEST_CASE("DiskErrors: [ERR-01] Propagate File Not Found") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  util_safe_strcpy(cmd.path, "nonexistent_file.dsk", disk_insert_path_max);

  // Command usually returns OK because it's queued, but here internal
  // synchronously executes for local tests.
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(SL6, disk_query_status, &status, &size);

  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive0_last_error ==
        static_cast<int32_t>(disk_err_file_not_found));

  linapple_shutdown();
}

TEST_CASE("DiskErrors: [ERR-02] Propagate Unsupported Format") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
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
  util_safe_strcpy(cmd.path, garbage, disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(SL6, disk_query_status, &status, &size);

  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive0_last_error ==
        static_cast<int32_t>(disk_err_unsupported_format));

  remove(garbage);
  linapple_shutdown();
}

TEST_CASE("DiskErrors: [ERR-03] Successful insertion clears error") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;

  // First, cause an error
  util_safe_strcpy(cmd.path, "missing.dsk", disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  // Now insert valid
  std::string fixture = TestFixtures::get_fixture_path("minimal.dsk");
  util_safe_strcpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(SL6, disk_query_status, &status, &size);

  CHECK(status.drive0_loaded != 0);
  CHECK(status.drive0_last_error == static_cast<int32_t>(disk_err_none));

  linapple_shutdown();
}

TEST_CASE("DiskErrors: [ERR-04] The loader answers a bad argument as one") {
  const DiskFormatDriver_t* driver =
      reinterpret_cast<const DiskFormatDriver_t*>(1);
  void* instance = reinterpret_cast<void*>(1);

  CHECK(disk_loader_open(nullptr, &driver, &instance) ==
        disk_err_invalid_argument);
  CHECK(driver == nullptr);
  CHECK(instance == nullptr);

  CHECK(disk_loader_open("/tmp", nullptr, &instance) ==
        disk_err_invalid_argument);
  CHECK(disk_loader_create(nullptr, "DOS Order") ==
        disk_err_invalid_argument);
}
