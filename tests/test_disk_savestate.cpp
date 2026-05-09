#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Common_Globals.h"
#include "core/Util_Text.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/disk/Disk.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <unistd.h>

namespace {
constexpr int SL6 = 6;
constexpr size_t DSK_140K_SIZE = 143360;
}

TEST_CASE("DiskSaveState: [SS-01] Round-trip fidelity") {
    Linapple_Init();
    Peripheral_Manager_Init();
    Peripheral_Register_Internal();

    // Insert a disk
    DiskInsertCmd_t cmd{};
    cmd.drive = DISK_DRIVE_0;
    const char* fixture = "../tests/fixtures/minimal.dsk";
    if (access(fixture, F_OK) != 0) fixture = "tests/fixtures/minimal.dsk";
    Util_SafeStrCpy(cmd.path, fixture, DISK_INSERT_PATH_MAX);
    Peripheral_Command(SL6, DISK_CMD_INSERT, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    DiskStatus_t status{};
    size_t s_size = sizeof(status);
    Peripheral_Query(SL6, DISK_CMD_GET_STATUS, &status, &s_size);
    REQUIRE(status.drive0_loaded == true);

    // Save State
    size_t state_size = 0;
    Peripheral_SaveState(SL6, nullptr, &state_size);
    REQUIRE(state_size > 0);

    std::vector<uint8_t> buffer(state_size);
    Peripheral_SaveState(SL6, buffer.data(), &state_size);

    // Reset peripheral state
    Peripheral_Manager_Reset();
    Peripheral_Query(SL6, DISK_CMD_GET_STATUS, &status, &s_size);
    CHECK(status.drive0_loaded == false);

    // Restore State
    Peripheral_LoadState(SL6, buffer.data(), state_size);

    Peripheral_Query(SL6, DISK_CMD_GET_STATUS, &status, &s_size);
    CHECK(status.drive0_loaded == true);
    CHECK(status.drive0_last_error == DISK_ERR_NONE);
    CHECK(strstr(status.drive0_full_path, "minimal.dsk") != nullptr);

    Peripheral_Manager_Shutdown();
    Linapple_Shutdown();
}

TEST_CASE("DiskSaveState: [SS-02] Missing image on restore") {
    Linapple_Init();
    Peripheral_Manager_Init();
    Peripheral_Register_Internal();

    const char* temp_img = "to_be_deleted.dsk";
    {
        FilePtr f(fopen(temp_img, "wb"), fclose);
        std::vector<uint8_t> zero(DSK_140K_SIZE, 0);
        fwrite(zero.data(), 1, zero.size(), f.get());
    }

    DiskInsertCmd_t cmd{};
    cmd.drive = DISK_DRIVE_0;
    Util_SafeStrCpy(cmd.path, temp_img, DISK_INSERT_PATH_MAX);
    Peripheral_Command(SL6, DISK_CMD_INSERT, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    size_t state_size = 0;
    Peripheral_SaveState(SL6, nullptr, &state_size);
    std::vector<uint8_t> buffer(state_size);
    Peripheral_SaveState(SL6, buffer.data(), &state_size);

    // Make image unreachable
    unlink(temp_img);

    // Restore state
    Peripheral_LoadState(SL6, buffer.data(), state_size);

    DiskStatus_t status{};
    size_t s_size = sizeof(status);
    Peripheral_Query(SL6, DISK_CMD_GET_STATUS, &status, &s_size);

    // Should handle gracefully: not loaded, but reported error
    CHECK(status.drive0_loaded == false);
    CHECK(status.drive0_last_error == DISK_ERR_FILE_NOT_FOUND);

    Peripheral_Manager_Shutdown();
    Linapple_Shutdown();
}
