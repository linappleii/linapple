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
#include "core/Util_Crc32.h"
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
    "DiskWOZ1: the loader strips MacBinary and reads the records past it") {
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

namespace {

constexpr long info_version_offset = 20;

auto open_patched_v1_image(long offset, uint8_t value) -> DiskError_e {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  patch(image.path(), offset, &value, 1);

  void* instance = nullptr;
  const DiskError_e err =
      g_woz1_driver.open(image.c_str(), 0, false, &instance);
  if (instance != nullptr) {
    g_woz1_driver.close(instance);
  }
  return err;
}

}  // namespace

TEST_CASE("DiskWOZ1: an INFO disk type other than 5.25\" is unsupported") {
  CHECK(open_patched_v1_image(info_disk_type_offset, 0) ==
        disk_err_unsupported_format);
  CHECK(open_patched_v1_image(info_disk_type_offset, 3) ==
        disk_err_unsupported_format);
  CHECK(open_patched_v1_image(info_disk_type_offset, 1) == disk_err_none);
}

TEST_CASE("DiskWOZ1: a WOZ1 file accepts INFO version 1 only") {
  CHECK(open_patched_v1_image(info_version_offset, 0) ==
        disk_err_unsupported_format);
  CHECK(open_patched_v1_image(info_version_offset, 2) ==
        disk_err_unsupported_format);
  CHECK(open_patched_v1_image(info_version_offset, 1) == disk_err_none);
}

namespace {
constexpr long crc32_field_offset = 8;
constexpr uint32_t v1_fixture_crc32 = 0x3E9FC695;
}  // namespace

TEST_CASE("DiskWOZ1: a 1.0 image's CRC32 is verified over its chunks") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  const uint8_t crc_le[] = {0x95, 0xC6, 0x9F, 0x3E};
  patch(image.path(), crc32_field_offset, crc_le, sizeof(crc_le));

  void* instance = nullptr;
  REQUIRE(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);
  g_woz1_driver.close(instance);
  instance = nullptr;

  const uint8_t flipped = static_cast<uint8_t>(track0_pattern[0] ^ 0x80);
  patch(image.path(), 256, &flipped, 1);
  CHECK(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
        disk_err_corrupt);
  CHECK(instance == nullptr);
}

namespace {

constexpr uint32_t first_quarter_track_past_map = 160;
constexpr uint8_t third_record_index = 2;
constexpr size_t chunk_header_size = 8;
constexpr size_t meta_chunk_data_size = 32;
constexpr size_t woz_file_header_size = 12;

auto read_file(const std::string& path) -> std::vector<uint8_t> {
  FilePtr_t f(fopen(path.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  std::vector<uint8_t> data(static_cast<size_t>(Path::file_size(f.get())), 0);
  REQUIRE(fread(data.data(), 1, data.size(), f.get()) == data.size());
  return data;
}

// A META chunk after the records, as Applesauce writes one, with the CRC32
// the specification defines over every chunk, so open has to walk past META
// to accept the file at all.
auto append_meta_with_crc(const std::string& path) -> void {
  std::vector<uint8_t> out = read_file(path);
  REQUIRE(out.size() == v1_fixture_bytes);

  const uint8_t meta_header[] = {'M', 'E', 'T', 'A', meta_chunk_data_size,
                                 0,   0,   0};
  out.insert(out.end(), meta_header, meta_header + sizeof(meta_header));
  const char meta_text[] = "title\tminimal v1\n";
  std::vector<uint8_t> meta(meta_chunk_data_size, ' ');
  std::memcpy(meta.data(), meta_text, sizeof(meta_text) - 1);
  out.insert(out.end(), meta.begin(), meta.end());

  const uint32_t crc = crc32_compute(out.data() + woz_file_header_size,
                                     out.size() - woz_file_header_size);
  out[crc32_field_offset] = static_cast<uint8_t>(crc & 0xFF);
  out[crc32_field_offset + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  out[crc32_field_offset + 2] = static_cast<uint8_t>((crc >> 16) & 0xFF);
  out[crc32_field_offset + 3] = static_cast<uint8_t>(crc >> 24);

  FilePtr_t f(fopen(path.c_str(), "wb"), fclose);
  REQUIRE(f != nullptr);
  REQUIRE(fwrite(out.data(), 1, out.size(), f.get()) == out.size());
}

}  // namespace

TEST_CASE(
    "DiskWOZ1: a TMAP entry pointing at a record with no cells is corrupt") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  const uint8_t no_cells[] = {0, 0};
  patch(image.path(), track0_bit_count_offset, no_cells, sizeof(no_cells));

  void* instance = nullptr;
  REQUIRE(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz1_driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_corrupt);
  CHECK(bit_count == 0);
  CHECK(bit_timing == 32);
  CHECK(bits[0] == 0xEE);

  CHECK(read_quarter_track(g_woz1_driver, instance, 4, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == track1_bit_count);

  g_woz1_driver.close(instance);
}

TEST_CASE("DiskWOZ1: a quarter track past the map is a bad argument") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  void* instance = nullptr;
  REQUIRE(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz1_driver, instance,
                           first_quarter_track_past_map, &bits, &bit_count,
                           &bit_timing) == disk_err_invalid_argument);
  CHECK(bit_count == 0);
  CHECK(bit_timing == 32);
  CHECK(bits[0] == 0xEE);

  CHECK(read_quarter_track(g_woz1_driver, instance, UINT32_MAX, &bits,
                           &bit_count,
                           &bit_timing) == disk_err_invalid_argument);
  CHECK(bit_count == 0);
  CHECK(bits[0] == 0xEE);

  g_woz1_driver.close(instance);
}

TEST_CASE("DiskWOZ1: a META chunk after TRKS leaves the record count alone") {
  auto image = TestFixtures::create_ephemeral("minimal-v1.woz");
  patch(image.path(), tmap_entry_8_offset, &third_record_index, 1);
  append_meta_with_crc(image.path());
  REQUIRE(file_size_of(image.path()) ==
          v1_fixture_bytes + chunk_header_size + meta_chunk_data_size);

  void* instance = nullptr;
  REQUIRE(g_woz1_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);
  REQUIRE(instance != nullptr);

  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  CHECK(read_quarter_track(g_woz1_driver, instance, 0, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == track0_bit_count);
  CHECK(std::memcmp(bits.data(), track0_pattern, sizeof(track0_pattern)) == 0);

  CHECK(read_quarter_track(g_woz1_driver, instance, 4, &bits, &bit_count,
                           &bit_timing) == disk_err_none);
  CHECK(bit_count == track1_bit_count);
  CHECK(std::memcmp(bits.data(), track1_pattern, sizeof(track1_pattern)) == 0);

  // The META bytes sit where a third record would start; TRKS still declares
  // two, so the entry naming a third is corrupt rather than a read of META.
  CHECK(read_quarter_track(g_woz1_driver, instance, 8, &bits, &bit_count,
                           &bit_timing) == disk_err_corrupt);
  CHECK(bit_count == 0);
  CHECK(bits[0] == 0xEE);

  g_woz1_driver.close(instance);
}
