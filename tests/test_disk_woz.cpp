// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/formats/Woz2Driver.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr int slot_6 = 6;
constexpr size_t woz_header_size = 1536;
constexpr size_t woz_data_block_size = 512;

auto write_all_zero_woz2(const char* path) -> void {
  FilePtr_t f(fopen(path, "wb"), fclose);
  REQUIRE(f != nullptr);

  std::vector<uint8_t> hdr(woz_header_size, 0);
  std::memcpy(hdr.data(), "WOZ2\xFF\n\r\n", 8);

  // INFO chunk: size 60, disk_type 1 (5.25")
  std::memcpy(hdr.data() + 12, "INFO", 4);
  hdr[16] = 60;
  hdr[21] = 1;

  // TMAP chunk: size 160, entry 0 -> track 0 uses trks_index 0
  std::memcpy(hdr.data() + 80, "TMAP", 4);
  hdr[84] = 160;
  std::memset(hdr.data() + 88, 0xFF, 160);
  hdr[88] = 0;

  // TRKS chunk: size 1280 (0x0500)
  std::memcpy(hdr.data() + 248, "TRKS", 4);
  hdr[252] = 0x00;
  hdr[253] = 0x05;

  // TRKS entry 0: starting_block 3, block_count 1, bit_count 4096
  hdr[256] = 3;
  hdr[257] = 0;
  hdr[258] = 1;
  hdr[259] = 0;
  const uint32_t bit_count = static_cast<uint32_t>(woz_data_block_size * 8);
  std::memcpy(hdr.data() + 260, &bit_count, sizeof(bit_count));

  REQUIRE(fwrite(hdr.data(), 1, hdr.size(), f.get()) == hdr.size());

  // Block 3: 512 bytes of zeros
  const std::vector<uint8_t> zero_block(woz_data_block_size, 0);
  REQUIRE(fwrite(zero_block.data(), 1, zero_block.size(), f.get()) ==
          zero_block.size());
}

}  // namespace

TEST_CASE("DiskIntegration: [INT-04] WOZ Integration Check") {
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  auto ephemeral_disk = TestFixtures::create_ephemeral("minimal.woz");
  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  cmd.write_protected = false;
  util_safe_strcpy(cmd.path, ephemeral_disk.c_str(), disk_insert_path_max);
  peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(slot_6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded != 0);
  CHECK(std::strcmp(status.drive0_full_path, ephemeral_disk.c_str()) == 0);
  CHECK(status.drive0_write_protected != 0);

  linapple_shutdown();
}

TEST_CASE("DiskWOZ: [WOZ-3] All-zero bitstream does not infinite loop") {
  TestFixtures::ScopedTempFile_t temp_woz(".woz");
  write_all_zero_woz2(temp_woz.c_str());

  void* instance = nullptr;
  bool is_ro = false;
  DiskError_e err =
      g_woz2_driver.open(temp_woz.c_str(), 0, 0, &is_ro, &instance);
  REQUIRE(err == disk_err_none);
  REQUIRE(instance != nullptr);

  std::vector<uint8_t> track_buffer(nibbles_per_track, 0);
  int nibbles_read = -1;
  g_woz2_driver.read_track(instance, 0, 0, track_buffer.data(), &nibbles_read);

  CHECK(nibbles_read == 1);
  CHECK(track_buffer[0] == 0);

  g_woz2_driver.close(instance);
}

TEST_CASE(
    "DiskWOZ: [WOZ-1/2] Corrupted chunk size does not loop or crash open") {
  TestFixtures::ScopedTempFile_t corrupted_file(".woz");
  {
    FilePtr_t f(fopen(corrupted_file.c_str(), "wb"), fclose);
    REQUIRE(f != nullptr);
    std::vector<uint8_t> hdr(woz_header_size, 0);
    // Write WOZ2 header
    std::memcpy(hdr.data(), "WOZ2\xFF\n\r\n", 8);
    // Write corrupted chunk header with huge chunk_size that overflows uint32
    hdr[12] = 'I';
    hdr[13] = 'N';
    hdr[14] = 'F';
    hdr[15] = 'O';
    hdr[16] = 0xFF;
    hdr[17] = 0xFF;
    hdr[18] = 0xFF;
    hdr[19] = 0xFF;  // chunk_size = 0xFFFFFFFF
    REQUIRE(fwrite(hdr.data(), 1, hdr.size(), f.get()) == hdr.size());
  }

  void* instance = nullptr;
  bool is_ro = false;
  DiskError_e err =
      g_woz2_driver.open(corrupted_file.c_str(), 0, 0, &is_ro, &instance);
  CHECK(err == disk_err_corrupt);
  CHECK(instance == nullptr);
}
