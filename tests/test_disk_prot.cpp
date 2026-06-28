#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "core/Util_Path.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/disk/Disk.h"
#include "apple2/CPU.h"
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <climits>
#include <vector>

namespace {
constexpr int SL6 = 6;

static void setup_smoke_test(const char* imagePath) {
    Linapple_Init();
    if (imagePath) {
        Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1, imagePath);
    }
    Peripheral_Manager_Init(); 
    Linapple_RegisterPeripherals();
}
}

TEST_CASE("DiskIntegration: [PROT-01] Three-Layer Write Protection") {
    // 1. Determine absolute paths for fixtures
    char* cwd_raw = get_current_dir_name();
    std::string repo_root = cwd_raw;
    free(cwd_raw);
    
    // If we are in a build directory, go up one level
    size_t build_pos = repo_root.find("/build");
    if (build_pos != std::string::npos) {
        repo_root = repo_root.substr(0, build_pos);
    }

    std::string fixture_dir = repo_root + "/tests/fixtures";
    std::string fixture_woz = fixture_dir + "/minimal.woz";
    std::string fixture_dsk = fixture_dir + "/minimal.dsk";

    std::string f_user = repo_root + "/user_prot.dsk";
    std::string f_os = repo_root + "/os_prot.dsk";
    std::string f_format = repo_root + "/format_prot.woz";
    std::string f_rw = repo_root + "/rw.dsk";

    setup_smoke_test(fixture_woz.c_str());

    auto copy_fix = [](const std::string& src_p, const std::string& dst_p, size_t size) {
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
    Util_SafeStrCpy(cmd.path, f_user.c_str(), disk_insert_path_max);
    cmd.write_protected = true;
    Peripheral_Command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);
    Peripheral_Query(SL6, disk_cmd_get_status, &status, &size);
    CHECK(status.drive0_loaded != 0);
    CHECK(status.drive0_write_protected != 0);

    // Layer 2: OS Read-Only
    if (getuid() != 0) {
        chmod(f_os.c_str(), 0444);
        Util_SafeStrCpy(cmd.path, f_os.c_str(), disk_insert_path_max);
        cmd.write_protected = false;
        Peripheral_Command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
        Peripheral_Manager_Think(0);
        Peripheral_Query(SL6, disk_cmd_get_status, &status, &size);
        CHECK(status.drive0_loaded != 0);
        CHECK(status.drive0_write_protected != 0);
    }

    // Layer 1: Format/Driver Capability
    Util_SafeStrCpy(cmd.path, f_format.c_str(), disk_insert_path_max);
    cmd.write_protected = false;
    Peripheral_Command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);
    Peripheral_Query(SL6, disk_cmd_get_status, &status, &size);
    CHECK(status.drive0_loaded != 0);
    CHECK(status.drive0_write_protected == 0);

    // All clear: Writable
    Util_SafeStrCpy(cmd.path, f_rw.c_str(), disk_insert_path_max);
    cmd.write_protected = false;
    Peripheral_Command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);
    Peripheral_Query(SL6, disk_cmd_get_status, &status, &size);
    CHECK(status.drive0_loaded != 0);
    CHECK(status.drive0_write_protected == 0);

    remove(f_user.c_str());
    remove(f_os.c_str());
    remove(f_format.c_str());
    remove(f_rw.c_str());
    Linapple_Shutdown();
}
