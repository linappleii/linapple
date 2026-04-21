#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Common_Globals.h"
#include "core/Registry.h"
#include "apple2/DiskCommands.h"
#include "apple2/Disk.h"
#include <cstring>

TEST_CASE("DiskIntegration: [INT-05] Runtime Eject Clears Config") {
    Linapple_Init();
    Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1, "../tests/fixtures/minimal.woz");
    Linapple_RegisterPeripherals();

    DiskEjectCmd_t cmd{};
    cmd.drive = DISK_DRIVE_0;

    Peripheral_Command(6, DISK_CMD_EJECT, &cmd, sizeof(cmd));
    Peripheral_Manager_Think(0);

    std::string saved = Configuration::Instance().GetString("Slots", REGVALUE_DISK_IMAGE1);
    CHECK(saved.empty());

    Peripheral_Manager_Shutdown();
    Linapple_Shutdown();
}
