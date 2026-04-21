#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Common_Globals.h"
#include "core/Util_Text.h"
#include "apple2/DiskCommands.h"
#include "apple2/Memory.h"
#include "apple2/CPU.h"
#include "apple2/Disk.h"
#include <cstring>

namespace {
constexpr int SL6 = 6;
constexpr int CYCLES_PER_FRAME = 17030;
constexpr int MOTOR_ON_SWITCH = 0xC0E9;
constexpr int MOTOR_OFF_SWITCH = 0xC0E8;
constexpr int MOTOR_SPIN_DURATION = 2500000;
}

void run_cycles(uint64_t cycles) {
    uint64_t count = 0;
    while(count < cycles) {
        uint32_t chunk = (cycles - count > static_cast<uint64_t>(CYCLES_PER_FRAME))
            ? static_cast<uint32_t>(CYCLES_PER_FRAME)
            : static_cast<uint32_t>(cycles - count);
        Linapple_RunFrame(chunk);
        count += chunk;
    }
}

TEST_CASE("DiskIntegration: [INT-03] Motor Activity Notification") {
    Linapple_Init();
    Linapple_RegisterPeripherals();

    // Set PC to a safe loop: $0000: 4C 00 00 (JMP $0000)
    uint8_t* m = MemGetBankPtr(0);
    m[0] = 0x4C;
    m[1] = 0x00;
    m[2] = 0x00;
    regs.pc = 0x0000;
    g_state.mode = MODE_RUNNING;

    DiskInsertCmd_t cmd{};
    cmd.drive = DISK_DRIVE_0;
    cmd.write_protected = false;
    Util_SafeStrCpy(cmd.path, "../tests/fixtures/minimal.woz", DISK_INSERT_PATH_MAX);
    Peripheral_Command(SL6, DISK_CMD_INSERT, &cmd, sizeof(cmd));

    Peripheral_Manager_Think(0);

    CHECK(Peripheral_IsAnyActive() == false);

    IOMap_Dispatch(0, MOTOR_ON_SWITCH, 0, 0, 0);
    run_cycles(100000);
    CHECK(Peripheral_IsAnyActive() == true);

    IOMap_Dispatch(0, MOTOR_OFF_SWITCH, 0, 0, 0);
    run_cycles(MOTOR_SPIN_DURATION);
    CHECK(Peripheral_IsAnyActive() == false);

    Peripheral_Manager_Shutdown();
    Linapple_Shutdown();
}
