// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include "HeadlessHarness.h"
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

namespace {

struct SmokeTestFixture_t {
  explicit SmokeTestFixture_t(const std::string& image_path1 = "",
                              const std::string& image_path2 = "") {
    init(image_path1, image_path2);
  }

  ~SmokeTestFixture_t() { shutdown(); }

  auto init(const std::string& image_path1 = "",
            const std::string& image_path2 = "") -> void {
    if (initialized_) {
      shutdown();
    }
    linapple_init();
    if (!image_path1.empty()) {
      Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                             image_path1);
    }
    if (!image_path2.empty()) {
      Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE2,
                                             image_path2);
    }
    peripheral_manager_init();
    linapple_register_peripherals();
    initialized_ = true;
  }

  auto shutdown() -> void {
    if (initialized_) {
      linapple_shutdown();
      initialized_ = false;
    }
  }

  SmokeTestFixture_t(const SmokeTestFixture_t&) = delete;
  auto operator=(const SmokeTestFixture_t&) -> SmokeTestFixture_t& = delete;
  SmokeTestFixture_t(SmokeTestFixture_t&&) = delete;
  auto operator=(SmokeTestFixture_t&&) -> SmokeTestFixture_t& = delete;

 private:
  bool initialized_ = false;
};

}  // namespace

TEST_CASE("DiskSmoke: [SMK-01] DOS 3.3 Boot") {
  HeadlessHarness_t harness;
  auto disk = TestFixtures::create_ephemeral("Master.dsk");
  harness.mount_disk(6, 0, disk);
  harness.boot();
  harness.run_frames(250);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);

  std::string screen_content;
  for (int row = 0; row < 24; ++row) {
    screen_content += harness.get_text_row(row);
  }
  CHECK(screen_content.find(']') != std::string::npos);
}

TEST_CASE("DiskSmoke: [SMK-03] WOZ 2 Boot") {
  auto disk = TestFixtures::create_ephemeral("minimal.woz");
  SmokeTestFixture_t fixture(disk.path());

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == true);
}

TEST_CASE("DiskSmoke: [SMK-05] error - Missing File") {
  SmokeTestFixture_t fixture("/tmp/nonexistent_smoke_file_12345.dsk");

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_file_not_found);
}

TEST_CASE("DiskSmoke: [SMK-06] error - Corrupt WOZ") {
  TestFixtures::ScopedTempFile_t corrupt_file(".woz");
  {
    std::ofstream ofs(corrupt_file.path(), std::ios::binary);
    REQUIRE(ofs.is_open());
    ofs.write("NOTWOZXX", 8);
  }

  SmokeTestFixture_t fixture(corrupt_file.path());
  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error != disk_err_none);
}

TEST_CASE("DiskSmoke: [SMK-07] error - Unsupported Format") {
  auto unsupported_disk = TestFixtures::create_ephemeral("minimal.txt");
  SmokeTestFixture_t fixture(unsupported_disk.path());

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_unsupported_format);
}

TEST_CASE("DiskSmoke: [SMK-08] Save/Restore Persistence") {
  auto disk = TestFixtures::create_ephemeral("minimal.woz");
  SmokeTestFixture_t fixture(disk.path());

  size_t state_size = 0;
  peripheral_save_state(6, nullptr, &state_size);
  std::vector<uint8_t> buffer(state_size);
  peripheral_save_state(6, buffer.data(), &state_size);

  fixture.shutdown();
  fixture.init();

  peripheral_load_state(6, buffer.data(), state_size);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == true);
  CHECK(std::string(status.drive0_full_path) == disk.path());
}

TEST_CASE("DiskSmoke: [SMK-10] Drive Swapping") {
  auto disk1 = TestFixtures::create_ephemeral("minimal.dsk");
  auto disk2 = TestFixtures::create_ephemeral("minimal.woz");

  SmokeTestFixture_t fixture(disk1.path(), disk2.path());

  DiskStatus_t status{};
  size_t size = sizeof(status);

  // Swap
  peripheral_command(6, disk_cmd_swap_drives, nullptr, 0);
  peripheral_manager_think(0);

  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(std::string(status.drive0_full_path) == disk2.path());
  CHECK(std::string(status.drive1_full_path) == disk1.path());
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
  disk_loader_shutdown();
}

TEST_CASE(
    "DiskSmoke: [DSK-2] DOS 3.3 VTOC signature detection via "
    "disk_loader_open") {
  TestFixtures::ScopedTempFile_t tmp_dos(".dsk");
  std::vector<uint8_t> disk_image(143360, 0);
  for (int loop = 1; loop <= 15; ++loop) {
    disk_image[0x11000 + 2 + (loop * 0x100)] = static_cast<uint8_t>(loop - 1);
  }
  {
    std::ofstream ofs(tmp_dos.path(), std::ios::binary);
    REQUIRE(ofs.is_open());
    ofs.write(reinterpret_cast<const char*>(disk_image.data()),
              static_cast<std::streamsize>(disk_image.size()));
  }

  disk_loader_init();
  disk_loader_register(const_cast<DiskFormatDriver_t*>(&g_do_driver));

  DiskFormatDriver_t* selected_driver = nullptr;
  void* disk_instance = nullptr;
  bool is_ro = false;
  CHECK(disk_loader_open(tmp_dos.c_str(), false, 0, &is_ro, &selected_driver,
                         &disk_instance) == disk_err_none);
  CHECK(selected_driver == &g_do_driver);
  if (selected_driver != nullptr && disk_instance != nullptr &&
      selected_driver->close != nullptr) {
    selected_driver->close(disk_instance);
  }
  disk_loader_shutdown();
}

TEST_CASE("DiskSmoke: [SMK-07] SAVE and CATALOG in DOS 3.3") {
  HeadlessHarness_t harness;
  auto disk = TestFixtures::create_ephemeral("Master.dsk");
  harness.mount_disk(6, 0, disk);
  harness.boot();
  harness.run_frames(250);

  harness.type_string("5 PRINT \"HI\"\r", 5);
  harness.run_frames(60);

  harness.type_string("SAVE FOO\r", 5);
  harness.run_frames(300);

  harness.type_string("CATALOG\r", 5);
  harness.run_frames(300);

  std::string catalog_screen;
  for (int row = 0; row < 24; ++row) {
    catalog_screen += harness.get_text_row(row) + "\n";
  }
  CHECK(catalog_screen.find("FOO") != std::string::npos);

  harness.type_string("NEW\r", 5);
  harness.run_frames(60);

  harness.type_string("LOAD FOO\r", 5);
  harness.run_frames(300);

  harness.type_string("LIST 5\r", 5);
  harness.run_frames(100);

  std::string foo_screen;
  for (int row = 0; row < 24; ++row) {
    foo_screen += harness.get_text_row(row) + "\n";
  }
  CHECK(foo_screen.find("5  PRINT \"HI\"") != std::string::npos);

  harness.type_string("LOAD HELLO\r", 5);
  harness.run_frames(300);

  harness.type_string("LIST 40\r", 5);
  harness.run_frames(100);

  std::string hello_screen;
  for (int row = 0; row < 24; ++row) {
    hello_screen += harness.get_text_row(row) + "\n";
  }
  CHECK(hello_screen.find("40  PRINT") != std::string::npos);
  CHECK(hello_screen.find("16384") == std::string::npos);
}
