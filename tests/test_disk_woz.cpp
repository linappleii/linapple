// SPDX-License-Identifier: GPL-2.0-only
#include <string>

#include "Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/formats/Woz2Driver.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {
constexpr int SL6 = 6;
}

TEST_CASE("DiskIntegration: [INT-04] WOZ Integration Check") {
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();
  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  cmd.write_protected = false;
  std::string fixture = TestFixtures::get_fixture_path("minimal.woz");
  util_safe_strcpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(SL6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded != 0);
  CHECK(strstr(status.drive0_full_path, "minimal.woz") != nullptr);
  CHECK(status.drive0_write_protected != 0);

  linapple_shutdown();
}

extern auto reconstruct_bitstream_nibble(const uint8_t* buffer,
                                         uint32_t bit_count,
                                         uint32_t* bit_idx_ptr) -> uint8_t;

TEST_CASE("DiskWOZ: [WOZ-3] All-zero bitstream does not infinite loop") {
  std::vector<uint8_t> zeros(512, 0);
  uint32_t bit_idx = 0;
  uint8_t nibble =
      reconstruct_bitstream_nibble(zeros.data(), zeros.size() * 8, &bit_idx);
  CHECK(nibble == 0);
}

TEST_CASE(
    "DiskWOZ: [WOZ-1/2] Corrupted chunk size does not loop or crash open") {
  const char* temp_woz = "corrupted_chunk.woz";
  {
    FilePtr_t f(fopen(temp_woz, "wb"), fclose);
    std::vector<uint8_t> hdr(1536, 0);
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
    fwrite(hdr.data(), 1, hdr.size(), f.get());
  }

  void* instance = nullptr;
  bool is_ro = false;
  DiskError_e err = g_woz2_driver.open(temp_woz, 0, 0, &is_ro, &instance);
  CHECK(err == disk_err_corrupt);
  CHECK(instance == nullptr);

  unlink(temp_woz);
}
