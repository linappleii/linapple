// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include <cstdint>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {
constexpr int SL6 = 6;

static void setup_smoke_test(const char* imagePath) {
  linapple_init();
  if (imagePath) {
    Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1,
                                           imagePath);
  }
  peripheral_manager_init();
  linapple_register_peripherals();
}
}  // namespace

TEST_CASE("DiskIntegration: [PROT-01] Three-Layer Write Protection") {
  std::string fixture_woz = TestFixtures::get_fixture_path("minimal.woz");
  std::string fixture_dsk = TestFixtures::get_fixture_path("minimal.dsk");

  std::string f_user = "user_prot.dsk";
  std::string f_os = "os_prot.dsk";
  std::string f_format = "format_prot.woz";
  std::string f_rw = "rw.dsk";

  setup_smoke_test(fixture_woz.c_str());

  auto copy_fix = [](const std::string& src_p, const std::string& dst_p,
                     size_t size) {
    FILE* src = fopen(src_p.c_str(), "rb");
    REQUIRE_MESSAGE(src != nullptr, "Could not open source fixture: " << src_p);

    FILE* dst = fopen(dst_p.c_str(), "wb");
    REQUIRE_MESSAGE(dst != nullptr, "Could not open destination: " << dst_p);

    std::vector<uint8_t> buf(size);
    REQUIRE(fread(buf.data(), 1, size, src) == size);
    fwrite(buf.data(), 1, size, dst);
    fclose(src);
    fclose(dst);
  };

  copy_fix(fixture_dsk, f_user, 143360);
  copy_fix(fixture_dsk, f_os, 143360);
  copy_fix(fixture_woz, f_format, 1536);
  copy_fix(fixture_dsk, f_rw, 143360);

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  DiskStatus_t status{};
  size_t size = sizeof(status);

  // Layer 3: User Toggle
  util_safe_strcpy(cmd.path, f_user.c_str(), disk_insert_path_max);
  cmd.write_protected = true;
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);
  peripheral_query(SL6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded != 0);
  CHECK(status.drive0_write_protected != 0);

  // Layer 2: OS Read-Only
  if (getuid() != 0) {
    chmod(f_os.c_str(), 0444);
    util_safe_strcpy(cmd.path, f_os.c_str(), disk_insert_path_max);
    cmd.write_protected = false;
    peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
    peripheral_manager_think(0);
    peripheral_query(SL6, disk_cmd_get_status, &status, &size);
    CHECK(status.drive0_loaded != 0);
    CHECK(status.drive0_write_protected != 0);
  }

  // Layer 1: Format/Driver Capability
  util_safe_strcpy(cmd.path, f_format.c_str(), disk_insert_path_max);
  cmd.write_protected = false;
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);
  peripheral_query(SL6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded != 0);
  CHECK(status.drive0_write_protected != 0);

  // All clear: Writable
  util_safe_strcpy(cmd.path, f_rw.c_str(), disk_insert_path_max);
  cmd.write_protected = false;
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);
  peripheral_query(SL6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded != 0);
  CHECK(status.drive0_write_protected == 0);

  remove(f_user.c_str());
  remove(f_os.c_str());
  remove(f_format.c_str());
  remove(f_rw.c_str());
  linapple_shutdown();
}
