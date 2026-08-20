#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "doctest.h"

// Global helper for smoke tests
static void setup_smoke_test(const char* imagePath) {
  linapple_init();
  if (imagePath) {
    Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                           imagePath);
  }
  peripheral_manager_init();  // Clear auto-registered cards
  peripheral_manager_init();
  linapple_register_peripherals();  // Re-register with new config
}

static void teardown_smoke_test() { linapple_shutdown(); }

TEST_CASE("DiskSmoke: [SMK-01] DOS 3.3 Boot") {
  setup_smoke_test(
      "../tests/fixtures/minimal.dsk");  // Actually our fixture is just a 140k
                                         // blank but it has DSK structure
  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);
  teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-03] WOZ 2 Boot") {
  setup_smoke_test("../tests/fixtures/minimal.woz");
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
  setup_smoke_test("../tests/fixtures/minimal.txt");
  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_unsupported_format);
  teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-08] Save/Restore Persistence") {
  linapple_init();
  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                         "../tests/fixtures/minimal.woz");
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
  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                         "../tests/fixtures/minimal.dsk");
  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE2,
                                         "../tests/fixtures/minimal.woz");
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
