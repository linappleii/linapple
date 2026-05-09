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

TEST_CASE("DiskIntegration: [INT-01] Startup Config Loading") {
    Linapple_Init();
    Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1, "../tests/fixtures/minimal.woz");

    Peripheral_Manager_Init();
    Peripheral_Manager_Init(); Linapple_RegisterPeripherals();

    DiskStatus_t status{};
    size_t size = sizeof(status);
    PeripheralStatus ps = Peripheral_Query(6, DISK_CMD_GET_STATUS, &status, &size);

    REQUIRE(ps == PERIPHERAL_OK);
    CHECK(status.drive0_loaded == true);

    Peripheral_Manager_Shutdown();
    Linapple_Shutdown();
}

TEST_CASE("DiskIntegration: [INT-02] Missing Startup Image") {
    Linapple_Init();
    Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1, "nonexistent.dsk");

    Peripheral_Manager_Init();
    Peripheral_Manager_Init(); Linapple_RegisterPeripherals();

    DiskStatus_t status{};
    size_t size = sizeof(status);
    PeripheralStatus ps = Peripheral_Query(6, DISK_CMD_GET_STATUS, &status, &size);

    REQUIRE(ps == PERIPHERAL_OK);
    CHECK(status.drive0_loaded == false);
    CHECK(status.drive0_last_error == DISK_ERR_FILE_NOT_FOUND);

    Peripheral_Manager_Shutdown();
    Linapple_Shutdown();
}
