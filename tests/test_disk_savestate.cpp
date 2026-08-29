#include <string>
#include <cstdint>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <vector>

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
constexpr size_t DSK_140K_SIZE = 143360;
}  // namespace

TEST_CASE("DiskSaveState: [SS-01] Round-trip fidelity") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  // Insert a disk
  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  std::string fixture = TestFixtures::get_fixture_path("minimal.dsk");
  Util_SafeStrCpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t s_size = sizeof(status);
  peripheral_query(SL6, disk_cmd_get_status, &status, &s_size);
  REQUIRE(status.drive0_loaded == true);

  // Save State
  size_t state_size = 0;
  peripheral_save_state(SL6, nullptr, &state_size);
  REQUIRE(state_size > 0);

  std::vector<uint8_t> buffer(state_size);
  peripheral_save_state(SL6, buffer.data(), &state_size);

  // Reset peripheral state
  peripheral_manager_reset();
  peripheral_query(SL6, disk_cmd_get_status, &status, &s_size);
  CHECK(status.drive0_loaded == false);

  // Restore State
  peripheral_load_state(SL6, buffer.data(), state_size);

  peripheral_query(SL6, disk_cmd_get_status, &status, &s_size);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);
  CHECK(strstr(status.drive0_full_path, "minimal.dsk") != nullptr);

  linapple_shutdown();
}

TEST_CASE("DiskSaveState: [SS-02] Missing image on restore") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  const char* temp_img = "to_be_deleted.dsk";
  {
    FilePtr_t f(fopen(temp_img, "wb"), fclose);
    std::vector<uint8_t> zero(DSK_140K_SIZE, 0);
    fwrite(zero.data(), 1, zero.size(), f.get());
  }

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  Util_SafeStrCpy(cmd.path, temp_img, disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  size_t state_size = 0;
  peripheral_save_state(SL6, nullptr, &state_size);
  std::vector<uint8_t> buffer(state_size);
  peripheral_save_state(SL6, buffer.data(), &state_size);

  // Make image unreachable
  unlink(temp_img);

  // Restore state
  peripheral_load_state(SL6, buffer.data(), state_size);

  DiskStatus_t status{};
  size_t s_size = sizeof(status);
  peripheral_query(SL6, disk_cmd_get_status, &status, &s_size);

  // Should handle gracefully: not loaded, but reported error
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_file_not_found);

  linapple_shutdown();
}
