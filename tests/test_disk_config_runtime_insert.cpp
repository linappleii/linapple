#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Common_Globals.h"
#include "core/Registry.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/Disk.h"
#include <cstring>

TEST_CASE("DiskIntegration: [INT-04] Runtime Insert Updates Config") {
    Linapple_Init();
    Peripheral_Manager_Init(); Linapple_RegisterPeripherals();

    // Initial state: empty
    Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1, "");

    DiskInsertCmd_t cmd{};
    cmd.drive = DISK_DRIVE_0;
    strcpy(cmd.path, "../tests/fixtures/minimal.woz");

    Peripheral_Command(6, DISK_CMD_INSERT, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    std::string saved = Configuration::Instance().GetString("Slots", REGVALUE_DISK_IMAGE1);
    CHECK(saved == "../tests/fixtures/minimal.woz");

    DiskStatus_t status{};
    size_t size = sizeof(status);
    PeripheralStatus ps = Peripheral_Query(6, DISK_CMD_GET_STATUS, &status, &size);
    REQUIRE(ps == PERIPHERAL_OK);
    CHECK(status.drive0_loaded == true);
    CHECK(status.drive0_last_error == DISK_ERR_NONE);

    Linapple_Shutdown();
}
