#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Common_Globals.h"
#include "core/Util_Text.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/Disk.h"
#include <cstring>

namespace {
constexpr int SL6 = 6;
}

TEST_CASE("DiskIntegration: [INT-04] WOZ Integration Check") {
    Linapple_Init();
    Peripheral_Manager_Init(); Linapple_RegisterPeripherals();
    DiskInsertCmd_t cmd{};
    cmd.drive = DISK_DRIVE_0;
    cmd.write_protected = false;
    Util_SafeStrCpy(cmd.path, "../tests/fixtures/minimal.woz", DISK_INSERT_PATH_MAX);
    Peripheral_Command(SL6, DISK_CMD_INSERT, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    PeripheralStatus ps = Peripheral_Query(SL6, DISK_CMD_GET_STATUS, &status, &size);

    REQUIRE(ps == PERIPHERAL_OK);
    CHECK(status.drive0_loaded != 0);
    CHECK(strstr(status.drive0_full_path, "minimal.woz") != nullptr);
    CHECK(status.drive0_write_protected == 0);

    Linapple_Shutdown();
}
