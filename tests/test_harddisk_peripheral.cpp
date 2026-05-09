#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "apple2/Harddisk.h"
#include "apple2/HarddiskCommands.h"
#include "apple2/Memory.h"
#include <vector>
#include <cstring>

extern Peripheral_t g_harddisk_peripheral;

TEST_CASE("Harddisk Peripheral: Lifecycle and Registration") {
    Linapple_Init();
    Peripheral_Manager_Init();

    // Register Harddisk in Slot 7
    int result = Peripheral_Register(&g_harddisk_peripheral, 7);
    CHECK(result == 0);

    // Verify I/O mapping for $C0F0
    // We expect some value from the harddisk IO handler.
    // By default, without an image, it should return DEVICE_OK (0x00) for most reads.
    uint8_t val = IOMap_Dispatch(0, 0xC0F2, 0, 0, 0); // Read Command register
    CHECK(val == 0);

    Linapple_Shutdown();
}

TEST_CASE("Harddisk Peripheral: Commands and Queries") {
    Linapple_Init();
    Peripheral_Manager_Init();
    Peripheral_Register(&g_harddisk_peripheral, 7);

    // 1. Check initial status
    HarddiskStatus_t status;
    size_t size = sizeof(status);
    PeripheralStatus pstatus = Peripheral_Query(7, HARDDISK_CMD_GET_STATUS, &status, &size);
    CHECK(pstatus == PERIPHERAL_OK);
    CHECK(status.drive0_loaded == 0);
    CHECK(status.drive1_loaded == 0);

    // 2. Test INSERT command (with a non-existent file to check error reporting)
    HarddiskInsertCmd_t insert;
    memset(&insert, 0, sizeof(insert));
    insert.drive = 0;
    strncpy(insert.path, "non_existent_image.hdv", sizeof(insert.path) - 1);
    
    pstatus = Peripheral_Command(7, HARDDISK_CMD_INSERT, &insert, sizeof(insert));
    CHECK(pstatus == PERIPHERAL_OK);
    
    // Commands are processed during Think
    Peripheral_Manager_Think(0);

    // 3. Verify error in status
    size = sizeof(status);
    pstatus = Peripheral_Query(7, HARDDISK_CMD_GET_STATUS, &status, &size);
    CHECK(pstatus == PERIPHERAL_OK);
    CHECK(status.drive0_loaded == 0);
    CHECK(status.drive0_last_error != 0); // HARDDISK_ERR_NOT_FOUND

    // 4. Test EJECT command
    HarddiskEjectCmd_t eject;
    eject.drive = 0;
    pstatus = Peripheral_Command(7, HARDDISK_CMD_EJECT, &eject, sizeof(eject));
    CHECK(pstatus == PERIPHERAL_OK);
    Peripheral_Manager_Think(0);

    size = sizeof(status);
    pstatus = Peripheral_Query(7, HARDDISK_CMD_GET_STATUS, &status, &size);
    CHECK(status.drive0_last_error == 0); // Reset after eject/cleanup

    Linapple_Shutdown();
}

TEST_CASE("Harddisk Peripheral: Direct I/O Logic") {
    Linapple_Init();
    Peripheral_Manager_Init();
    Peripheral_Register(&g_harddisk_peripheral, 7);

    // Write Unit Number to $C0F3
    IOMap_Dispatch(0, 0xC0F3, 1, 0x80, 0); // Drive 2
    
    // Read it back
    uint8_t val = IOMap_Dispatch(0, 0xC0F3, 0, 0, 0);
    CHECK(val == 0x80);

    // Write Command to $C0F2
    IOMap_Dispatch(0, 0xC0F2, 1, 0x01, 0); // Read command
    val = IOMap_Dispatch(0, 0xC0F2, 0, 0, 0);
    CHECK(val == 0x01);

    Linapple_Shutdown();
}
