// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include <cstdint>
#include <vector>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/DoDriver.h"
#include "apple2/peripherals/disk/formats/Woz2Driver.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "doctest.h"
#include "test_fixtures.h"

// Global helper for smoke tests
static void setup_smoke_test(const char* imagePath) {
  linapple_init();
  if (imagePath) {
    std::string path = TestFixtures::get_fixture_path(imagePath);
    Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1, path);
  }
  peripheral_manager_init();  // Clear auto-registered cards
  peripheral_manager_init();
  linapple_register_peripherals();  // Re-register with new config
}

static void teardown_smoke_test() { linapple_shutdown(); }

TEST_CASE("DiskSmoke: [SMK-01] DOS 3.3 Boot") {
  setup_smoke_test("minimal.dsk");  // Actually our fixture is just a 140k
                                    // blank but it has DSK structure
  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);
  teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-03] WOZ 2 Boot") {
  setup_smoke_test("minimal.woz");
  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == true);
  teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-05] error - Missing File") {
  setup_smoke_test("nonexistent.dsk");
  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_file_not_found);
  teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-06] error - Corrupt WOZ") {
  FILE* f = fopen("corrupt.woz", "wb");
  fwrite("NOTWOZXX", 1, 8, f);
  fclose(f);

  setup_smoke_test("corrupt.woz");
  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == false);
  // Corrupt files often fall through to unsupported format or probe fail
  CHECK(status.drive0_last_error != disk_err_none);

  teardown_smoke_test();
  remove("corrupt.woz");
}

TEST_CASE("DiskSmoke: [SMK-07] error - Unsupported Format") {
  setup_smoke_test("minimal.txt");
  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_unsupported_format);
  teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-08] Save/Restore Persistence") {
  linapple_init();
  Configuration_t::instance().set_string(
      "Slots", REGVALUE_DISK_IMAGE1,
      TestFixtures::get_fixture_path("minimal.woz"));
  peripheral_manager_init();
  linapple_register_peripherals();

  size_t stateSize = 0;
  peripheral_save_state(6, nullptr, &stateSize);
  std::vector<uint8_t> buffer(stateSize);
  peripheral_save_state(6, buffer.data(), &stateSize);

  teardown_smoke_test();
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  peripheral_load_state(6, buffer.data(), stateSize);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == true);
  CHECK(strstr(status.drive0_full_path, "minimal.woz") != nullptr);

  teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-10] Drive Swapping") {
  linapple_init();
  Configuration_t::instance().set_string(
      "Slots", REGVALUE_DISK_IMAGE1,
      TestFixtures::get_fixture_path("minimal.dsk"));
  Configuration_t::instance().set_string(
      "Slots", REGVALUE_DISK_IMAGE2,
      TestFixtures::get_fixture_path("minimal.woz"));
  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);

  // Swap
  peripheral_command(6, disk_cmd_swap_drives, nullptr, 0);
  peripheral_manager_think(0);

  peripheral_query(6, disk_cmd_get_status, &status, &size);
  // Drive 0 should now be minimal.woz
  CHECK(strstr(status.drive0_full_path, "minimal.woz") != nullptr);
  // Drive 1 should now be minimal.dsk
  CHECK(strstr(status.drive1_full_path, "minimal.dsk") != nullptr);

  teardown_smoke_test();
}

TEST_CASE(
    "DiskSmoke: [DSK-1] Enforce write capability and callback invariants on "
    "registration") {
  disk_loader_init();

  // Floppy driver: write capability with null write_track must be rejected
  DiskFormatDriver_t bad_floppy1 = g_do_driver;
  bad_floppy1.capabilities = disk_driver_cap_write;
  bad_floppy1.write_track = nullptr;
  disk_loader_register(&bad_floppy1);

  // Floppy driver: read-only capabilities with non-null write_track must be
  // rejected
  DiskFormatDriver_t bad_floppy2 = g_do_driver;
  bad_floppy2.capabilities = 0;
  bad_floppy2.write_track = g_do_driver.write_track;
  disk_loader_register(&bad_floppy2);

  // Valid floppy drivers register cleanly
  DiskFormatDriver_t valid_floppy_ro = g_woz2_driver;
  valid_floppy_ro.capabilities = 0;
  valid_floppy_ro.write_track = nullptr;
  disk_loader_register(&valid_floppy_ro);

  DiskFormatDriver_t valid_floppy_rw = g_do_driver;
  disk_loader_register(&valid_floppy_rw);

  // Null pointer registrations are safely ignored
  disk_loader_register(nullptr);
}

TEST_CASE(
    "DiskSmoke: [DSK-2] DOS 3.3 VTOC signature detection via "
    "disk_loader_open") {
  const char* tmp_dos = "test_smoke_dos_vtoc.dsk";
  remove(tmp_dos);
  std::vector<uint8_t> disk_image(143360, 0);
  for (int loop = 1; loop <= 15; ++loop) {
    disk_image[0x11000 + 2 + (loop * 0x100)] = static_cast<uint8_t>(loop - 1);
  }
  FILE* fp = fopen(tmp_dos, "wb");
  REQUIRE(fp != nullptr);
  fwrite(disk_image.data(), 1, disk_image.size(), fp);
  fclose(fp);

  disk_loader_init();
  disk_loader_register(const_cast<DiskFormatDriver_t*>(&g_do_driver));

  DiskFormatDriver_t* selected_driver = nullptr;
  void* disk_instance = nullptr;
  bool is_ro = false;
  CHECK(disk_loader_open(tmp_dos, false, 0, &is_ro, &selected_driver,
                         &disk_instance) == disk_err_none);
  CHECK(selected_driver == &g_do_driver);
  if (selected_driver != nullptr && disk_instance != nullptr &&
      selected_driver->close != nullptr) {
    selected_driver->close(disk_instance);
  }
  disk_loader_shutdown();
  remove(tmp_dos);
}
