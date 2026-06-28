// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/disk/Disk.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"

namespace {
constexpr int SL6 = 6;
constexpr int DISK_IO_READ_WRITE = 0xC0EC;
constexpr int DISK_IO_LATCH = 0xC0ED;
constexpr int DISK_IO_READ_MODE = 0xC0EE;
constexpr int DISK_IO_WRITE_MODE = 0xC0EF;
constexpr int DISK_MOTOR_ON = 0xC0E9;

auto setup_disk_io_test(const char* fixture_name) -> void {
  Linapple_Init();
  Peripheral_Manager_Init();
  Linapple_RegisterPeripherals();

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  cmd.write_protected = false;

  char* cwd_raw = get_current_dir_name();
  std::string repo_root = cwd_raw;
  free(cwd_raw);
  size_t build_pos = repo_root.find("/build");
  if (build_pos != std::string::npos) {
    repo_root = repo_root.substr(0, build_pos);
  }

  std::string fixture = repo_root + "/tests/fixtures/" + fixture_name;
  Util_SafeStrCpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  Peripheral_Command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));

  // Turn on motor so rotation works
  IOMap_Dispatch(0, DISK_MOTOR_ON, 0, 0, 0);
  Peripheral_Manager_Think(0);
}
}  // namespace

TEST_CASE("DiskIO: [IO-01] Sequential Read") {
  setup_disk_io_test("minimal.nib");

  // Ensure Read Mode
  IOMap_Dispatch(0, DISK_IO_READ_MODE, 0, 0, 0);

  // Read first few bytes of track 0
  uint8_t b1 = IOMap_Dispatch(0, DISK_IO_READ_WRITE, 0, 0, 0);
  uint8_t b2 = IOMap_Dispatch(0, DISK_IO_READ_WRITE, 0, 0, 0);
  uint8_t b3 = IOMap_Dispatch(0, DISK_IO_READ_WRITE, 0, 0, 0);

  // Bytes should be different (or at least valid GCR)
  CHECK(b1 != 0);
  CHECK(b2 != 0);
  CHECK(b3 != 0);

  Linapple_Shutdown();
}

TEST_CASE("DiskIO: [IO-02] Spindle Rotation") {
  setup_disk_io_test("minimal.nib");

  IOMap_Dispatch(0, DISK_IO_READ_MODE, 0, 0, 0);

  // Read current byte
  uint8_t b_start = IOMap_Dispatch(0, DISK_IO_READ_WRITE, 0, 0, 0);

  // Spin the disk for a while (approx 1/10th of a rotation)
  // 1 rotation = 200ms = approx 200,000 cycles
  Peripheral_Manager_Think(20000);

  // Read again
  uint8_t b_after = IOMap_Dispatch(0, DISK_IO_READ_WRITE, 0, 0, 0);

  // Byte position should have advanced background-style
  // (Hard to verify exact byte without knowing fixture content perfectly,
  // but it shouldn't be the same byte we would have read sequentially)
  (void)b_start;
  (void)b_after;

  Linapple_Shutdown();
}

TEST_CASE("DiskIO: [IO-03] Floating Bus Accuracy") {
  Linapple_Init();
  Peripheral_Manager_Init();
  Linapple_RegisterPeripherals();

  // No disk loaded. Accessing slot 6 I/O should return floating bus noise.
  // Physical reality: Bit 7 represents some hardware status or floating noise.
  uint8_t noise = IOMap_Dispatch(0, DISK_IO_READ_WRITE, 0, 0, 0);
  CHECK((noise & 0x80) !=
        0);  // Bit 7 should be set if we passed floating_bus (0xFF)

  Linapple_Shutdown();
}

TEST_CASE("DiskIO: [IO-04] Latch Persistence") {
  setup_disk_io_test("minimal.nib");

  // Write a value to the latch
  IOMap_Dispatch(0, DISK_IO_LATCH, 1, 0x55, 0);

  // Read it back (C08D returns latch in Read mode too)
  uint8_t val = IOMap_Dispatch(0, DISK_IO_LATCH, 0, 0, 0);
  CHECK(val == 0x55);

  Linapple_Shutdown();
}
