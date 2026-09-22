// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/Woz1Driver.h"
#include "apple2/peripherals/disk/formats/Woz2Driver.h"
#include "core/Util_Path.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr size_t woz1_probe_size = 256;
constexpr uint32_t track0_bit_count = 51200;
constexpr uint32_t track1_bit_count = 50001;
constexpr uint32_t woz1_max_bit_count = 6646 * 8;

// Three ten-cell self-sync nibbles then the address prologue, as cells rather
// than nibbles, which is why the bytes past the first FF look nothing like FF.
const uint8_t track0_pattern[] = {0xFF, 0x3F, 0xCF, 0xF3, 0x56, 0xAA, 0x58};
const uint8_t track1_pattern[] = {0xAA, 0xD5, 0xAA, 0xAD};

constexpr long info_disk_type_offset = 21;
constexpr long tmap_id_offset = 80;
constexpr long tmap_entry_8_offset = 96;
constexpr long track0_bit_count_offset = 6904;
constexpr long track1_record_offset = 6912;

auto patch(const std::string& path, long offset, const uint8_t* bytes,
           size_t len) -> void {
  FilePtr_t f(fopen(path.c_str(), "r+b"), fclose);
  REQUIRE(f != nullptr);
  REQUIRE(fseek(f.get(), offset, SEEK_SET) == 0);
  REQUIRE(fwrite(bytes, 1, len, f.get()) == len);
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

auto load_header(const std::string& path, size_t len) -> std::vector<uint8_t> {
  std::vector<uint8_t> header(len, 0);
  FilePtr_t f(fopen(path.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  REQUIRE(fread(header.data(), 1, len, f.get()) == len);
  return header;
}

}  // namespace

TEST_CASE("DiskWOZ1: the loader hands a 1.0 image to the WOZ 1 driver") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  disk_loader_reset();

  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(image.c_str(), &driver, &instance) == disk_err_none);
  REQUIRE(driver != nullptr);
  REQUIRE(instance != nullptr);
  CHECK(std::string(driver->name) == "WOZ 1");
  CHECK(driver == &g_woz1_driver);
  CHECK(driver->is_write_protected(instance) == false);
  driver->close(instance);
}

TEST_CASE("DiskWOZ1: mapped quarter tracks read the record's exact cells") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  void* instance = nullptr;
  REQUIRE(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;

  SUBCASE("quarter track 0 is track 0") {
    CHECK(read_quarter_track(g_woz1_driver, instance, 0, &bits, &bit_count,
                             &bit_timing) == disk_err_none);
    CHECK(bit_count == track0_bit_count);
    CHECK(bit_timing == 32);
    CHECK(std::memcmp(bits.data(), track0_pattern, sizeof(track0_pattern)) ==
          0);
    for (size_t i = sizeof(track0_pattern); i < track0_bit_count / 8; ++i) {
      REQUIRE(bits[i] == 0);
    }
  }

  SUBCASE("quarter track 3 is still track 0") {
    CHECK(read_quarter_track(g_woz1_driver, instance, 3, &bits, &bit_count,
                             &bit_timing) == disk_err_none);
    CHECK(bit_count == track0_bit_count);
    CHECK(std::memcmp(bits.data(), track0_pattern, sizeof(track0_pattern)) ==
          0);
  }

  SUBCASE("quarter track 4 is track 1, with a bit count off a byte edge") {
    CHECK(read_quarter_track(g_woz1_driver, instance, 4, &bits, &bit_count,
                             &bit_timing) == disk_err_none);
    CHECK(bit_count == track1_bit_count);
    CHECK(bit_timing == 32);
    CHECK(std::memcmp(bits.data(), track1_pattern, sizeof(track1_pattern)) ==
          0);
    CHECK(bits[(track1_bit_count + 7) / 8 - 1] == 0);
  }

  SUBCASE("quarter track 8 is unmapped and reads as no cells at all") {
    CHECK(read_quarter_track(g_woz1_driver, instance, 8, &bits, &bit_count,
                             &bit_timing) == disk_err_none);
    CHECK(bit_count == 0);
    CHECK(bit_timing == 32);
  }

  g_woz1_driver.close(instance);
}

TEST_CASE("DiskWOZ1: the driver offers no writer") {
  CHECK(g_woz1_driver.write_track_bits == nullptr);
  CHECK(g_woz1_driver.create == nullptr);
  CHECK((g_woz1_driver.capabilities & disk_driver_cap_write) == 0);
}

TEST_CASE("DiskWOZ1: the two WOZ drivers refuse each other's magic") {
  const std::string v1_path = TestFixtures::get_fixture_path("minimal-v1.woz");
  const std::string v2_path = TestFixtures::get_fixture_path("minimal.woz");
  const std::vector<uint8_t> v1 = load_header(v1_path, woz1_probe_size);
  const std::vector<uint8_t> v2 = load_header(v2_path, 1536);

  CHECK(g_woz1_driver.probe(v1.data(), v1.size(), 13568, ".woz") ==
        disk_probe_definite);
  CHECK(g_woz2_driver.probe(v1.data(), v1.size(), 13568, ".woz") ==
        disk_probe_no);
  CHECK(g_woz2_driver.probe(v2.data(), v2.size(), 1536, ".woz") ==
        disk_probe_definite);
  CHECK(g_woz1_driver.probe(v2.data(), v2.size(), 1536, ".woz") ==
        disk_probe_no);

  auto v2_image = TestFixtures::create_ephemeral("minimal.woz");
  disk_loader_reset();
  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(v2_image.c_str(), &driver, &instance) ==
          disk_err_none);
  CHECK(driver == &g_woz2_driver);
  driver->close(instance);
}

TEST_CASE("DiskWOZ1: a file cut short of a record is corrupt at that track") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  REQUIRE(truncate(image.c_str(), track1_record_offset + 100) == 0);

  void* instance = nullptr;
  REQUIRE(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz1_driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == track0_bit_count);
  CHECK(read_quarter_track(g_woz1_driver, instance, 4, &bits, &bit_count,
                           &bit_timing) == disk_err_corrupt);
  CHECK(bit_count == 0);
  g_woz1_driver.close(instance);
}

TEST_CASE("DiskWOZ1: a file cut short of its chunk headers will not open") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  REQUIRE(truncate(image.c_str(), 100) == 0);

  void* instance = nullptr;
  CHECK(g_woz1_driver.open(image.c_str(), 0, false, &instance) == disk_err_io);
  CHECK(instance == nullptr);
}

TEST_CASE("DiskWOZ1: a TMAP entry past the last record is corrupt") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  const uint8_t third_record = 2;
  patch(image.c_str(), tmap_entry_8_offset, &third_record, 1);

  void* instance = nullptr;
  REQUIRE(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz1_driver, instance, 8, &bits, &bit_count,
                           &bit_timing) == disk_err_corrupt);
  CHECK(bit_count == 0);
  g_woz1_driver.close(instance);
}

TEST_CASE("DiskWOZ1: a bit count past the record's cells is corrupt") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  const uint32_t too_many = woz1_max_bit_count + 1;
  const uint8_t bit_count_le[] = {static_cast<uint8_t>(too_many & 0xFF),
                                  static_cast<uint8_t>(too_many >> 8)};
  patch(image.c_str(), track0_bit_count_offset, bit_count_le, 2);

  void* instance = nullptr;
  REQUIRE(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz1_driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_corrupt);
  CHECK(bit_count == 0);
  g_woz1_driver.close(instance);
}

TEST_CASE("DiskWOZ1: a 3.5\" INFO disk type is an unsupported format") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  const uint8_t disk_type_3_5 = 2;
  patch(image.c_str(), info_disk_type_offset, &disk_type_3_5, 1);

  void* instance = nullptr;
  CHECK(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
        disk_err_unsupported_format);
  CHECK(instance == nullptr);
}

TEST_CASE("DiskWOZ1: a missing TMAP chunk is corrupt") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  const uint8_t not_a_map[] = {'X', 'X', 'X', 'X'};
  patch(image.c_str(), tmap_id_offset, not_a_map, sizeof(not_a_map));

  void* instance = nullptr;
  CHECK(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
        disk_err_corrupt);
  CHECK(instance == nullptr);
}

namespace {

constexpr uint32_t macbinary_header_size = 128;
constexpr uint32_t v1_fixture_bytes = 13568;

auto file_size_of(const std::string& path) -> uint32_t {
  FilePtr_t f(fopen(path.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  return static_cast<uint32_t>(Path::file_size(f.get()));
}

auto check_wrapped_track_reads(const DiskFormatDriver_t& driver, void* instance)
    -> void {
  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;

  CHECK(read_quarter_track(driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == track0_bit_count);
  CHECK(bit_timing == 32);
  CHECK(std::memcmp(bits.data(), track0_pattern, sizeof(track0_pattern)) == 0);
  CHECK(bits[0] == 0xFF);
  for (size_t i = sizeof(track0_pattern); i < track0_bit_count / 8; ++i) {
    REQUIRE(bits[i] == 0);
  }

  CHECK(read_quarter_track(driver, instance, 4, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == track1_bit_count);
  CHECK(bit_timing == 32);
  CHECK(std::memcmp(bits.data(), track1_pattern, sizeof(track1_pattern)) == 0);

  CHECK(read_quarter_track(driver, instance, 8, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == 0);
  CHECK(bit_timing == 32);
}

}  // namespace

TEST_CASE(
    "DiskWOZ1: a MacBinary-wrapped image reads its records past the wrapper") {
  auto image = TestFixtures::create_ephemeral("minimal-macbinary-v1.woz");
  REQUIRE(file_size_of(image.path()) ==
          macbinary_header_size + v1_fixture_bytes);

  void* instance = nullptr;
  REQUIRE(g_woz1_driver.open(image.c_str(), macbinary_header_size, false,
                             &instance) == disk_err_none);
  REQUIRE(instance != nullptr);

  check_wrapped_track_reads(g_woz1_driver, instance);

  g_woz1_driver.close(instance);
}

TEST_CASE(
    "DiskWOZ1: a wrapped image cut one byte short of a record is corrupt") {
  auto image = TestFixtures::create_ephemeral("minimal-macbinary-v1.woz");
  REQUIRE(truncate(image.c_str(),
                   macbinary_header_size + v1_fixture_bytes - 1) == 0);

  void* instance = nullptr;
  REQUIRE(g_woz1_driver.open(image.c_str(), macbinary_header_size, false,
                             &instance) == disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz1_driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == track0_bit_count);
  CHECK(read_quarter_track(g_woz1_driver, instance, 4, &bits, &bit_count,
                           &bit_timing) == disk_err_corrupt);
  CHECK(bit_count == 0);

  g_woz1_driver.close(instance);
}

TEST_CASE(
    "DiskWOZ1: the loader strips MacBinary and reads the records past it" *
    doctest::skip(true) *
    doctest::description(
        "loader MacBinary II detection lands in a parallel lane")) {
  auto image = TestFixtures::create_ephemeral("minimal-macbinary-v1.woz");
  disk_loader_reset();

  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(image.c_str(), &driver, &instance) == disk_err_none);
  REQUIRE(driver == &g_woz1_driver);
  REQUIRE(instance != nullptr);

  check_wrapped_track_reads(*driver, instance);

  driver->close(instance);
}
