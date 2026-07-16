#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
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
    cmd.drive = disk_drive_0;
    cmd.write_protected = false;
    Util_SafeStrCpy(cmd.path, "../tests/fixtures/minimal.woz", disk_insert_path_max);
    peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    PeripheralStatus_t ps = peripheral_query(SL6, disk_cmd_get_status, &status, &size);

    REQUIRE(ps == peripheral_ok);
    CHECK(status.drive0_loaded != 0);
    CHECK(strstr(status.drive0_full_path, "minimal.woz") != nullptr);
    CHECK(status.drive0_write_protected == 0);

    Linapple_Shutdown();
}
