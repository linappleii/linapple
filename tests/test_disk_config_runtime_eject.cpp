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

TEST_CASE("DiskIntegration: [INT-05] Runtime Eject Clears Config") {
    Linapple_Init();
    Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1, "../tests/fixtures/minimal.woz");
    Peripheral_Manager_Init(); Linapple_RegisterPeripherals();

    DiskEjectCmd_t cmd{};
    cmd.drive = disk_drive_0;

    Peripheral_Command(6, disk_cmd_eject, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    std::string saved = Configuration::Instance().GetString("Slots", REGVALUE_DISK_IMAGE1);
    CHECK(saved.empty());

    Linapple_Shutdown();
}
