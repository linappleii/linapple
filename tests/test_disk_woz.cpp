#include <string>
#include "Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {
constexpr int SL6 = 6;
}

TEST_CASE("DiskIntegration: [INT-04] WOZ Integration Check") {
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();
  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  cmd.write_protected = false;
  std::string fixture = TestFixtures::get_fixture_path("minimal.woz");
  Util_SafeStrCpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(SL6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded != 0);
  CHECK(strstr(status.drive0_full_path, "minimal.woz") != nullptr);
  CHECK(status.drive0_write_protected == 0);

  linapple_shutdown();
}
