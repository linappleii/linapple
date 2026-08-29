#include <cstdint>
#include <string>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

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
    linapple_run_frame(chunk);
    count += chunk;
  }
}

TEST_CASE("DiskIntegration: [INT-03] Motor Activity Notification") {
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  // Set PC to a safe loop: $0000: 4C 00 00 (JMP $0000)
  uint8_t* m = mem_get_bank_ptr(0);
  m[0] = 0x4C;
  m[1] = 0x00;
  m[2] = 0x00;
  cpu_get_registers()->pc = 0x0000;
  g_state.mode = MODE_RUNNING;

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  cmd.write_protected = false;
  std::string fixture = TestFixtures::get_fixture_path("minimal.woz");
  Util_SafeStrCpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));

  peripheral_manager_think(0);

  CHECK(peripheral_is_any_active() == false);

  io_map_dispatch(0, MOTOR_ON_SWITCH, 0, 0, 0);
  run_cycles(100000);
  CHECK(peripheral_is_any_active() == true);

  io_map_dispatch(0, MOTOR_OFF_SWITCH, 0, 0, 0);
  run_cycles(MOTOR_SPIN_DURATION);
  CHECK(peripheral_is_any_active() == false);

  linapple_shutdown();
}
