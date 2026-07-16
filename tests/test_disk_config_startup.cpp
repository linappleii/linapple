#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Registry.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/Disk.h"
#include <cstring>

TEST_CASE("DiskIntegration: [INT-01] Startup Config Loading") {
    Linapple_Init();
    Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1, "../tests/fixtures/minimal.woz");

    Peripheral_Manager_Init();
    Peripheral_Manager_Init(); Linapple_RegisterPeripherals();

    DiskStatus_t status{};
    size_t size = sizeof(status);
    PeripheralStatus_t ps = peripheral_query(6, disk_cmd_get_status, &status, &size);

    REQUIRE(ps == peripheral_ok);
    CHECK(status.drive0_loaded == true);

    Linapple_Shutdown();
}

TEST_CASE("DiskIntegration: [INT-02] Missing Startup Image") {
    Linapple_Init();
    Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1, "nonexistent.dsk");

    Peripheral_Manager_Init();
    Peripheral_Manager_Init(); Linapple_RegisterPeripherals();

    DiskStatus_t status{};
    size_t size = sizeof(status);
    PeripheralStatus_t ps = peripheral_query(6, disk_cmd_get_status, &status, &size);

    REQUIRE(ps == peripheral_ok);
    CHECK(status.drive0_loaded == false);
    CHECK(status.drive0_last_error == disk_err_file_not_found);

    Linapple_Shutdown();
}
