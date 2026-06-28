#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/disk/Disk.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Util_Text.h"
#include "doctest.h"

namespace {
constexpr int SL6 = 6;
constexpr int CYCLES_PER_FRAME = 17030;
constexpr int MOTOR_ON_SWITCH = 0xC0E9;
constexpr int MOTOR_OFF_SWITCH = 0xC0E8;
constexpr int MOTOR_SPIN_DURATION = 2500000;
}  // namespace

void run_cycles(uint64_t cycles) {
  uint64_t count = 0;
  while (count < cycles) {
    uint32_t chunk = (cycles - count > static_cast<uint64_t>(CYCLES_PER_FRAME))
                         ? static_cast<uint32_t>(CYCLES_PER_FRAME)
                         : static_cast<uint32_t>(cycles - count);
    Linapple_RunFrame(chunk);
    count += chunk;
  }
}

TEST_CASE("DiskIntegration: [INT-03] Motor Activity Notification") {
  Linapple_Init();
  Peripheral_Manager_Init();
  Linapple_RegisterPeripherals();

  // Set PC to a safe loop: $0000: 4C 00 00 (JMP $0000)
  uint8_t* m = MemGetBankPtr(0);
  m[0] = 0x4C;
  m[1] = 0x00;
  m[2] = 0x00;
  CpuGetRegisters()->pc = 0x0000;
  g_state.mode = MODE_RUNNING;

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  cmd.write_protected = false;
  Util_SafeStrCpy(cmd.path, "../tests/fixtures/minimal.woz",
                  disk_insert_path_max);
  Peripheral_Command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));

  Peripheral_Manager_Think(0);

  CHECK(Peripheral_IsAnyActive() == false);

  IOMap_Dispatch(0, MOTOR_ON_SWITCH, 0, 0, 0);
  run_cycles(100000);
  CHECK(Peripheral_IsAnyActive() == true);

  IOMap_Dispatch(0, MOTOR_OFF_SWITCH, 0, 0, 0);
  run_cycles(MOTOR_SPIN_DURATION);
  CHECK(Peripheral_IsAnyActive() == false);

  Linapple_Shutdown();
}
