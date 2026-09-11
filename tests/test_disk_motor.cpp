// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>

#include "apple2/Memory.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr int slot_6 = 6;
constexpr int cycles_per_frame = 17030;
constexpr uint16_t motor_off_switch = 0xC0E8;
constexpr uint16_t motor_on_switch = 0xC0E9;
constexpr uint64_t motor_spin_duration = 2500000;
constexpr uint64_t motor_spin_settle_cycles = 100000;
constexpr uint16_t spin_loop_addr = 0x0800;
constexpr uint16_t spin_loop_len = 3;
constexpr uint8_t jmp_abs_opcode = 0x4C;

class DiskMotorHarness_t {
 public:
  DiskMotorHarness_t()
      : disk_fixture_(TestFixtures::create_ephemeral("minimal.woz")),
        spin_program_(".apl") {
    linapple_init();
    peripheral_manager_init();
    linapple_register_peripherals();

    setup_spin_loop();

    g_state.mode = MODE_RUNNING;

    mount_disk();
    peripheral_manager_think(0);
  }

  ~DiskMotorHarness_t() { linapple_shutdown(); }

  DiskMotorHarness_t(const DiskMotorHarness_t&) = delete;
  auto operator=(const DiskMotorHarness_t&) -> DiskMotorHarness_t& = delete;
  DiskMotorHarness_t(DiskMotorHarness_t&&) = delete;
  auto operator=(DiskMotorHarness_t&&) -> DiskMotorHarness_t& = delete;

  auto run_cycles(uint64_t cycles) -> void {
    uint64_t count = 0;
    while (count < cycles) {
      uint32_t chunk =
          (cycles - count > static_cast<uint64_t>(cycles_per_frame))
              ? static_cast<uint32_t>(cycles_per_frame)
              : static_cast<uint32_t>(cycles - count);
      linapple_run_frame(chunk);
      count += chunk;
    }
  }

  auto set_motor_on() -> void { io_map_dispatch(0, motor_on_switch, 0, 0, 0); }

  auto set_motor_off() -> void {
    io_map_dispatch(0, motor_off_switch, 0, 0, 0);
  }

  auto is_motor_active() const -> bool { return peripheral_is_any_active(); }

 private:
  auto setup_spin_loop() -> void {
    // Apple II binary (.apl) format:
    // [0..1]: load address in little-endian ($0800)
    // [2..3]: length in little-endian (3 bytes)
    // [4..6]: code payload (JMP $0800: 4C 00 08)
    const uint8_t apl_data[] = {
        static_cast<uint8_t>(spin_loop_addr & 0xFF),
        static_cast<uint8_t>((spin_loop_addr >> 8) & 0xFF),
        static_cast<uint8_t>(spin_loop_len & 0xFF),
        static_cast<uint8_t>((spin_loop_len >> 8) & 0xFF),
        jmp_abs_opcode,
        static_cast<uint8_t>(spin_loop_addr & 0xFF),
        static_cast<uint8_t>((spin_loop_addr >> 8) & 0xFF),
    };

    std::ofstream out(spin_program_.path(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      throw std::runtime_error(
          "Failed to open temporary .apl file for writing");
    }
    out.write(reinterpret_cast<const char*>(apl_data), sizeof(apl_data));
    out.close();

    int load_res = linapple_load_program(spin_program_.c_str());
    if (load_res != 0) {
      throw std::runtime_error(
          "Failed to load spin loop program via linapple_load_program");
    }
  }

  auto mount_disk() -> void {
    DiskInsertCmd_t cmd{};
    cmd.drive = disk_drive_0;
    cmd.write_protected = false;
    util_safe_strcpy(cmd.path, disk_fixture_.c_str(), disk_insert_path_max);
    peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
  }

  TestFixtures::EphemeralDiskFixture_t disk_fixture_;
  TestFixtures::ScopedTempFile_t spin_program_;
};

}  // namespace

TEST_CASE("DiskIntegration: [INT-03] Motor Activity Notification") {
  DiskMotorHarness_t harness;

  CHECK(harness.is_motor_active() == false);

  harness.set_motor_on();
  harness.run_cycles(motor_spin_settle_cycles);
  CHECK(harness.is_motor_active() == true);

  harness.set_motor_off();
  harness.run_cycles(motor_spin_duration);
  CHECK(harness.is_motor_active() == false);
}
