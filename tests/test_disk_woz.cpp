// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/peripherals/Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/Woz2Driver.h"
#include "core/LinAppleCore.h"
#include "core/Util_Crc32.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {
// Declared rather than inherited: with no configuration the slot fallbacks in
// peripheral_register_internal supply a printer, a Super Serial Card and a
// Mockingboard beside the Disk II, none of which these cases touch.
using TestConfig_t = TestFixtures::ScopedTestConfig_t;
}  // namespace

namespace {

constexpr int slot_6 = 6;
constexpr size_t woz_header_size = 1536;
constexpr size_t woz_data_block_size = 512;

auto write_zero_track_woz2(const char* path, uint16_t block_count,
                           uint32_t bit_count) -> void {
  FilePtr_t f(fopen(path, "wb"), fclose);
  REQUIRE(f != nullptr);

  std::vector<uint8_t> hdr(woz_header_size, 0);
  std::memcpy(hdr.data(), "WOZ2\xFF\n\r\n", 8);

  // INFO chunk: size 60, version 2, disk_type 1 (5.25")
  std::memcpy(hdr.data() + 12, "INFO", 4);
  hdr[16] = 60;
  hdr[20] = 2;
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

  // TRKS entry 0: starting_block 3, then the caller's block and bit counts
  hdr[256] = 3;
  hdr[257] = 0;
  hdr[258] = static_cast<uint8_t>(block_count & 0xFF);
  hdr[259] = static_cast<uint8_t>(block_count >> 8);
  hdr[260] = static_cast<uint8_t>(bit_count & 0xFF);
  hdr[261] = static_cast<uint8_t>((bit_count >> 8) & 0xFF);
  hdr[262] = static_cast<uint8_t>((bit_count >> 16) & 0xFF);
  hdr[263] = static_cast<uint8_t>(bit_count >> 24);

  REQUIRE(fwrite(hdr.data(), 1, hdr.size(), f.get()) == hdr.size());

  const std::vector<uint8_t> zero_blocks(woz_data_block_size * block_count, 0);
  REQUIRE(fwrite(zero_blocks.data(), 1, zero_blocks.size(), f.get()) ==
          zero_blocks.size());
}

auto write_all_zero_woz2(const char* path) -> void {
  write_zero_track_woz2(path, 1,
                        static_cast<uint32_t>(woz_data_block_size * 8));
}

}  // namespace

TEST_CASE("DiskIntegration: [INT-04] WOZ Integration Check") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
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
      peripheral_query(slot_6, disk_query_status, &status, &size);

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
  DiskError_e err = g_woz2_driver.open(temp_woz.c_str(), 0, false, &instance);
  REQUIRE(err == disk_err_none);
  REQUIRE(instance != nullptr);

  std::vector<uint8_t> bits(max_track_bits / 8, 0xFF);
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(g_woz2_driver.read_track_bits(instance, 0, bits.data(), max_track_bits,
                                      &bit_count,
                                      &bit_timing) == disk_err_none);

  CHECK(bit_count == woz_data_block_size * 8);
  CHECK(bit_timing == disk_default_bit_timing);
  for (uint32_t i = 0; i < bit_count / 8; ++i) {
    CHECK(bits[i] == 0);
  }

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
  DiskError_e err =
      g_woz2_driver.open(corrupted_file.c_str(), 0, false, &instance);
  CHECK(err == disk_err_corrupt);
  CHECK(instance == nullptr);
}

namespace {

constexpr uint32_t macbinary_header_size = 128;
constexpr uint32_t track_fixture_bit_count = 4096;
constexpr uint32_t track_fixture_bytes = 2048;
constexpr uint32_t unmapped_quarter_track = 4;
constexpr uint32_t last_quarter_track = 159;

// The first cells of minimal-track.woz's only record; a shifted read lands in
// the TRKS chunk padding instead and sees zeros.
const uint8_t track_fixture_pattern[] = {0x01, 0x08, 0x0F, 0x16,
                                         0x1D, 0x24, 0x2B, 0x32};

auto read_file(const std::string& path) -> std::vector<uint8_t> {
  FilePtr_t f(fopen(path.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  std::vector<uint8_t> data(static_cast<size_t>(Path::file_size(f.get())), 0);
  REQUIRE(fread(data.data(), 1, data.size(), f.get()) == data.size());
  return data;
}

auto read_quarter_track(const DiskFormatDriver_t& driver, void* instance,
                        uint32_t quarter_track, std::vector<uint8_t>* bits,
                        uint32_t* bit_count, uint8_t* bit_timing)
    -> DiskError_e {
  bits->assign(max_track_bits / 8, 0xEE);
  *bit_count = 0;
  *bit_timing = 0;
  return driver.read_track_bits(instance, quarter_track, bits->data(),
                                max_track_bits, bit_count, bit_timing);
}

auto check_wrapped_track_reads(const DiskFormatDriver_t& driver, void* instance)
    -> void {
  const std::vector<uint8_t> bare =
      read_file(TestFixtures::get_fixture_path("minimal-track.woz"));
  REQUIRE(bare.size() == track_fixture_bytes);
  const uint8_t* const bare_block_3 = bare.data() + woz_header_size;

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;

  CHECK(read_quarter_track(driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == track_fixture_bit_count);
  CHECK(bit_timing == 32);
  CHECK(std::memcmp(bits.data(), track_fixture_pattern,
                    sizeof(track_fixture_pattern)) == 0);
  CHECK(std::memcmp(bits.data(), bare_block_3, woz_data_block_size) == 0);
  CHECK(bits[0] == 0x01);
  CHECK(bits[woz_data_block_size - 1] == bare_block_3[woz_data_block_size - 1]);

  CHECK(read_quarter_track(driver, instance, unmapped_quarter_track, &bits,
                           &bit_count, &bit_timing) == disk_err_none);
  CHECK(bit_count == 0);
  CHECK(bit_timing == 32);

  CHECK(read_quarter_track(driver, instance, last_quarter_track, &bits,
                           &bit_count, &bit_timing) == disk_err_none);
  CHECK(bit_count == 0);
  CHECK(bit_timing == 32);
}

}  // namespace

TEST_CASE(
    "DiskWOZ: a MacBinary-wrapped image reads its tracks past the wrapper") {
  auto image = TestFixtures::create_ephemeral("minimal-macbinary.woz");
  REQUIRE(read_file(image.path()).size() ==
          macbinary_header_size + track_fixture_bytes);

  void* instance = nullptr;
  REQUIRE(g_woz2_driver.open(image.c_str(), macbinary_header_size, false,
                             &instance) == disk_err_none);
  REQUIRE(instance != nullptr);

  check_wrapped_track_reads(g_woz2_driver, instance);

  g_woz2_driver.close(instance);
}

TEST_CASE(
    "DiskWOZ: a wrapped image cut one byte short of its track is corrupt") {
  auto image = TestFixtures::create_ephemeral("minimal-macbinary.woz");
  REQUIRE(truncate(image.c_str(),
                   macbinary_header_size + track_fixture_bytes - 1) == 0);

  void* instance = nullptr;
  REQUIRE(g_woz2_driver.open(image.c_str(), macbinary_header_size, false,
                             &instance) == disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz2_driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_corrupt);
  CHECK(bit_count == 0);
  CHECK(bits[0] == 0xEE);

  g_woz2_driver.close(instance);
}

TEST_CASE("DiskWOZ: the loader strips MacBinary and reads the tracks past it") {
  auto image = TestFixtures::create_ephemeral("minimal-macbinary.woz");
  disk_loader_reset();

  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(image.c_str(), &driver, &instance) == disk_err_none);
  REQUIRE(driver == &g_woz2_driver);
  REQUIRE(instance != nullptr);

  check_wrapped_track_reads(*driver, instance);

  driver->close(instance);
}

namespace {

constexpr long info_version_offset = 20;
constexpr long info_disk_type_offset = 21;
constexpr long trks_entry_0_offset = 256;
constexpr uint16_t widest_block_span = max_track_bits / (512 * 8);

auto patch(const std::string& path, long offset, const uint8_t* bytes,
           size_t len) -> void {
  FilePtr_t f(fopen(path.c_str(), "r+b"), fclose);
  REQUIRE(f != nullptr);
  REQUIRE(fseek(f.get(), offset, SEEK_SET) == 0);
  REQUIRE(fwrite(bytes, 1, len, f.get()) == len);
}

auto open_patched_track_image(long offset, uint8_t value) -> DiskError_e {
  auto image = TestFixtures::create_ephemeral("minimal-track.woz");
  patch(image.path(), offset, &value, 1);

  void* instance = nullptr;
  const DiskError_e err =
      g_woz2_driver.open(image.c_str(), 0, false, &instance);
  if (err != disk_err_none) {
    CHECK(instance == nullptr);
  }
  if (instance != nullptr) {
    g_woz2_driver.close(instance);
  }
  return err;
}

}  // namespace

TEST_CASE("DiskWOZ: an INFO disk type other than 5.25\" is unsupported") {
  CHECK(open_patched_track_image(info_disk_type_offset, 0) ==
        disk_err_unsupported_format);
  CHECK(open_patched_track_image(info_disk_type_offset, 2) ==
        disk_err_unsupported_format);
  CHECK(open_patched_track_image(info_disk_type_offset, 3) ==
        disk_err_unsupported_format);
  CHECK(open_patched_track_image(info_disk_type_offset, 1) == disk_err_none);
}

TEST_CASE("DiskWOZ: a WOZ2 file accepts INFO versions 2 and 3 only") {
  CHECK(open_patched_track_image(info_version_offset, 1) ==
        disk_err_unsupported_format);
  CHECK(open_patched_track_image(info_version_offset, 0) ==
        disk_err_unsupported_format);
  CHECK(open_patched_track_image(info_version_offset, 4) ==
        disk_err_unsupported_format);
  CHECK(open_patched_track_image(info_version_offset, 2) == disk_err_none);
  CHECK(open_patched_track_image(info_version_offset, 3) == disk_err_none);
}

TEST_CASE("DiskWOZ: a 2.1 image's flux-only quarter track reads as no cells") {
  auto image = TestFixtures::create_ephemeral("woz21-flux.woz");

  void* instance = nullptr;
  REQUIRE(g_woz2_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);
  REQUIRE(instance != nullptr);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz2_driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == 0);
  CHECK(bit_timing == 32);
  CHECK(bits[0] == 0xEE);

  g_woz2_driver.close(instance);
}

TEST_CASE("DiskWOZ: a track that starts inside the header is corrupt") {
  auto image = TestFixtures::create_ephemeral("minimal-track.woz");
  const uint8_t header_block = 2;
  patch(image.path(), trks_entry_0_offset, &header_block, 1);

  void* instance = nullptr;
  REQUIRE(g_woz2_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz2_driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_corrupt);
  CHECK(bit_count == 0);
  CHECK(bits[0] == 0xEE);

  g_woz2_driver.close(instance);
}

TEST_CASE("DiskWOZ: a track may span as many blocks as the buffer holds") {
  TestFixtures::ScopedTempFile_t widest(".woz");
  write_zero_track_woz2(widest.c_str(), widest_block_span, max_track_bits);

  void* instance = nullptr;
  REQUIRE(g_woz2_driver.open(widest.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz2_driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == max_track_bits);
  CHECK(bits[0] == 0);
  CHECK(bits[max_track_bits / 8 - 1] == 0);

  g_woz2_driver.close(instance);
}

TEST_CASE("DiskWOZ: a track spanning one block too many is unsupported") {
  TestFixtures::ScopedTempFile_t too_wide(".woz");
  write_zero_track_woz2(too_wide.c_str(), widest_block_span + 1,
                        max_track_bits + 1);

  void* instance = nullptr;
  REQUIRE(g_woz2_driver.open(too_wide.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz2_driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_unsupported);
  CHECK(bit_count == 0);
  CHECK(bits[0] == 0xEE);

  g_woz2_driver.close(instance);
}

namespace {
constexpr uint32_t crc32_check_value = 0xCBF43926;
constexpr uint32_t track_fixture_crc32 = 0x710E9D2A;
constexpr size_t woz_file_header_size = 12;
}  // namespace

TEST_CASE("Crc32: the check value and a split stream agree with zlib's CRC") {
  const char check_input[] = "123456789";
  const size_t check_len = sizeof(check_input) - 1;
  CHECK(crc32_compute(check_input, check_len) == crc32_check_value);

  uint32_t state = crc32_init();
  state = crc32_update(state, check_input, 4);
  state = crc32_update(state, check_input + 4, check_len - 4);
  CHECK(crc32_final(state) == crc32_check_value);

  CHECK(crc32_compute(nullptr, 0) == 0);
  CHECK(crc32_compute(check_input, 0) == 0);
}

TEST_CASE("Crc32: a WOZ fixture's body hashes to the CRC its twin carries") {
  const std::vector<uint8_t> bare =
      read_file(TestFixtures::get_fixture_path("minimal-track.woz"));
  REQUIRE(bare.size() > woz_file_header_size);
  CHECK(crc32_compute(bare.data() + woz_file_header_size,
                      bare.size() - woz_file_header_size) ==
        track_fixture_crc32);
}
