#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Common_Globals.h"
#include "core/Registry.h"
#include "apple2/DiskCommands.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Global helper for smoke tests
static void setup_smoke_test(const char* imagePath) {
    Linapple_Init();
    if (imagePath) {
        Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1, imagePath);
    }
    Linapple_RegisterPeripherals();
}

static void teardown_smoke_test() {
    Peripheral_Manager_Shutdown();
    Linapple_Shutdown();
}

TEST_CASE("DiskSmoke: [SMK-01] DOS 3.3 Boot") {
    setup_smoke_test("../tests/fixtures/minimal.dsk"); // Actually our fixture is just a 140k blank but it has DSK structure
    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(6, DISK_CMD_GET_STATUS, &status, &size);
    CHECK(status.drive0_loaded == true);
    CHECK(status.drive0_last_error == DISK_ERR_NONE);
    teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-03] WOZ 2 Boot") {
    setup_smoke_test("../tests/fixtures/minimal.woz");
    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(6, DISK_CMD_GET_STATUS, &status, &size);
    CHECK(status.drive0_loaded == true);
    teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-05] Error - Missing File") {
    setup_smoke_test("nonexistent.dsk");
    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(6, DISK_CMD_GET_STATUS, &status, &size);
    CHECK(status.drive0_loaded == false);
    CHECK(status.drive0_last_error == DISK_ERR_FILE_NOT_FOUND);
    teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-06] Error - Corrupt WOZ") {
    FILE* f = fopen("corrupt.woz", "wb");
    fwrite("NOTWOZXX", 1, 8, f);
    fclose(f);

    setup_smoke_test("corrupt.woz");
    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(6, DISK_CMD_GET_STATUS, &status, &size);
    CHECK(status.drive0_loaded == false);
    // Corrupt files often fall through to unsupported format or probe fail
    CHECK(status.drive0_last_error != DISK_ERR_NONE);

    teardown_smoke_test();
    remove("corrupt.woz");
}

TEST_CASE("DiskSmoke: [SMK-07] Error - Unsupported Format") {
    setup_smoke_test("../tests/fixtures/minimal.txt");
    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(6, DISK_CMD_GET_STATUS, &status, &size);
    CHECK(status.drive0_loaded == false);
    CHECK(status.drive0_last_error == DISK_ERR_UNSUPPORTED_FORMAT);
    teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-08] Save/Restore Persistence") {
    Linapple_Init();
    Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1, "../tests/fixtures/minimal.woz");
    Linapple_RegisterPeripherals();

    size_t stateSize = 0;
    Peripheral_SaveState(6, nullptr, &stateSize);
    std::vector<uint8_t> buffer(stateSize);
    Peripheral_SaveState(6, buffer.data(), &stateSize);

    teardown_smoke_test();
    Linapple_Init();
    Linapple_RegisterPeripherals();

    Peripheral_LoadState(6, buffer.data(), stateSize);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(6, DISK_CMD_GET_STATUS, &status, &size);
    CHECK(status.drive0_loaded == true);
    CHECK(strstr(status.drive0_full_path, "minimal.woz") != nullptr);

    teardown_smoke_test();
}

TEST_CASE("DiskSmoke: [SMK-09] Runtime Write Protect") {
    setup_smoke_test("../tests/fixtures/minimal.woz");

    DiskSetProtectCmd_t pcmd{};
    pcmd.drive = DISK_DRIVE_0;
    pcmd.write_protected = 1;
    Peripheral_Command(6, DISK_CMD_SET_PROTECT, &pcmd, sizeof(pcmd));
    Peripheral_Manager_Think(0);

    DiskStatus_t status{};
    size_t size = sizeof(status);
    Peripheral_Query(6, DISK_CMD_GET_STATUS, &status, &size);
    CHECK(status.drive0_write_protected == 1);

    teardown_smoke_test();
}
