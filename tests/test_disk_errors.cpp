#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Common_Globals.h"
#include "core/Util_Text.h"
#include "apple2/DiskCommands.h"
#include "apple2/Disk.h"
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
    cmd.drive = DISK_DRIVE_0;
    Util_SafeStrCpy(cmd.path, "nonexistent_file.dsk", DISK_INSERT_PATH_MAX);

    // Command usually returns OK because it's queued, but here internal
    // synchronously executes for local tests.
    Peripheral_Command(SL6, DISK_CMD_INSERT, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(SL6, DISK_CMD_GET_STATUS, &status, &size);

    CHECK(status.drive0_loaded == 0);
    CHECK(status.drive0_last_error == static_cast<int32_t>(DISK_ERR_FILE_NOT_FOUND));

    Peripheral_Manager_Shutdown();
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
    cmd.drive = DISK_DRIVE_0;
    Util_SafeStrCpy(cmd.path, garbage, DISK_INSERT_PATH_MAX);
    Peripheral_Command(SL6, DISK_CMD_INSERT, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(SL6, DISK_CMD_GET_STATUS, &status, &size);

    CHECK(status.drive0_loaded == 0);
    CHECK(status.drive0_last_error == static_cast<int32_t>(DISK_ERR_UNSUPPORTED_FORMAT));

    remove(garbage);
    Peripheral_Manager_Shutdown();
    Linapple_Shutdown();
}

TEST_CASE("DiskErrors: [ERR-03] Successful insertion clears error") {
    Linapple_Init();
    Peripheral_Manager_Init();
    Peripheral_Register_Internal();

    DiskInsertCmd_t cmd{};
    cmd.drive = DISK_DRIVE_0;

    // First, cause an error
    Util_SafeStrCpy(cmd.path, "missing.dsk", DISK_INSERT_PATH_MAX);
    Peripheral_Command(SL6, DISK_CMD_INSERT, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    // Now insert valid
    const char* fixture = "../tests/fixtures/minimal.dsk";
    if (access(fixture, F_OK) != 0) fixture = "tests/fixtures/minimal.dsk";
    Util_SafeStrCpy(cmd.path, fixture, DISK_INSERT_PATH_MAX);
    Peripheral_Command(SL6, DISK_CMD_INSERT, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(SL6, DISK_CMD_GET_STATUS, &status, &size);

    CHECK(status.drive0_loaded != 0);
    CHECK(status.drive0_last_error == static_cast<int32_t>(DISK_ERR_NONE));

    Peripheral_Manager_Shutdown();
    Linapple_Shutdown();
}
