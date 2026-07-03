#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Util_Text.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/Disk.h"
#include <cstring>
#include <cstdio>
#include <vector>

namespace {
constexpr int SL6 = 6;
}

TEST_CASE("DiskErrors: [ERR-01] Propagate File Not Found") {
    Linapple_Init();
    Peripheral_Manager_Init();
    Peripheral_Register_Internal();

    DiskInsertCmd_t cmd{};
    cmd.drive = disk_drive_0;
    Util_SafeStrCpy(cmd.path, "nonexistent_file.dsk", disk_insert_path_max);

    // Command usually returns OK because it's queued, but here internal
    // synchronously executes for local tests.
    Peripheral_Command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(SL6, disk_cmd_get_status, &status, &size);

    CHECK(status.drive0_loaded == 0);
    CHECK(status.drive0_last_error == static_cast<int32_t>(disk_err_file_not_found));

    Linapple_Shutdown();
}

TEST_CASE("DiskErrors: [ERR-02] Propagate Unsupported Format") {
    Linapple_Init();
    Peripheral_Manager_Init();
    Peripheral_Register_Internal();

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
    Peripheral_Command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(SL6, disk_cmd_get_status, &status, &size);

    CHECK(status.drive0_loaded == 0);
    CHECK(status.drive0_last_error == static_cast<int32_t>(disk_err_unsupported_format));

    remove(garbage);
    Linapple_Shutdown();
}

TEST_CASE("DiskErrors: [ERR-03] Successful insertion clears error") {
    Linapple_Init();
    Peripheral_Manager_Init();
    Peripheral_Register_Internal();

    DiskInsertCmd_t cmd{};
    cmd.drive = disk_drive_0;

    // First, cause an error
    Util_SafeStrCpy(cmd.path, "missing.dsk", disk_insert_path_max);
    Peripheral_Command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    // Now insert valid
    const char* fixture = "../tests/fixtures/minimal.dsk";
    if (access(fixture, F_OK) != 0) fixture = "tests/fixtures/minimal.dsk";
    Util_SafeStrCpy(cmd.path, fixture, disk_insert_path_max);
    Peripheral_Command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(SL6, disk_cmd_get_status, &status, &size);

    CHECK(status.drive0_loaded != 0);
    CHECK(status.drive0_last_error == static_cast<int32_t>(disk_err_none));

    Linapple_Shutdown();
}
