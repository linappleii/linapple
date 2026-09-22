// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/DiskContainer.h"
#include "core/Util_Path.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr size_t macbinary_header_len = 128;
constexpr size_t macbinary_crc_offset = 124;
constexpr size_t dos_track_size = 4096;
constexpr size_t dsk_image_size = 143360;
constexpr uint32_t dsk_track_bit_count = 50464;

// The synthesised track opens with 48 self-sync nibbles, so the first address
// field starts at nibble 48: prologue, then volume 254, track 0, sector 0 and
// their checksum in 4-and-4, then the epilogue (Beneath Apple DOS, ch. 3).
constexpr size_t first_address_field_at = 48;
const uint8_t track0_sector0_address_field[] = {0xD5, 0xAA, 0x96, 0xFF, 0xFE,
                                                0xAA, 0xAA, 0xAA, 0xAA, 0xFF,
                                                0xFE, 0xDE, 0xAA, 0xEB};
// Gap 2 is six sync nibbles, then the data prologue and 343 nibbles of
// six-and-two, which for an all-zero sector are 343 x 0x96.
constexpr size_t first_data_field_at = first_address_field_at + 14 + 6;
constexpr uint8_t zero_sector_nibble = 0x96;

// CRC-16 of the MacBinary II standard: polynomial 0x1021, initial value 0.
auto macbinary_crc16(const uint8_t* data, size_t length) -> uint16_t {
  uint16_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
    for (int bit = 0; bit < 8; ++bit) {
      crc = ((crc & 0x8000) != 0) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                  : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

auto read_file(const std::string& path) -> std::vector<uint8_t> {
  FilePtr_t f(fopen(path.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  std::vector<uint8_t> data;
  std::array<uint8_t, 16384> chunk{};
  size_t got = 0;
  while ((got = fread(chunk.data(), 1, chunk.size(), f.get())) > 0) {
    data.insert(data.end(), chunk.begin(), chunk.begin() + got);
  }
  return data;
}

struct OpenedImage_t {
  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;

  explicit OpenedImage_t(const std::string& path) {
    REQUIRE(disk_loader_open(path.c_str(), &driver, &instance) ==
            disk_err_none);
    REQUIRE(driver != nullptr);
    REQUIRE(instance != nullptr);
  }
  ~OpenedImage_t() {
    if (driver != nullptr && instance != nullptr) {
      driver->close(instance);
    }
  }
  OpenedImage_t(const OpenedImage_t&) = delete;
  auto operator=(const OpenedImage_t&) -> OpenedImage_t& = delete;
  OpenedImage_t(OpenedImage_t&&) = delete;
  auto operator=(OpenedImage_t&&) -> OpenedImage_t& = delete;
};

struct Track_t {
  uint32_t bit_count = 0;
  std::vector<uint8_t> nibbles;
  std::vector<uint8_t> sectors;
};

auto read_track0(const OpenedImage_t& image) -> Track_t {
  Track_t track;
  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint8_t timing = 0;
  REQUIRE(image.driver->read_track_bits(image.instance, 0, bits.data(),
                                        max_track_bits, &track.bit_count,
                                        &timing) == disk_err_none);

  track.nibbles.assign(max_track_bits / 8, 0);
  uint32_t nibble_count = 0;
  REQUIRE(disk_encoding_bits_to_nibbles(
              bits.data(), track.bit_count, track.nibbles.data(),
              static_cast<uint32_t>(track.nibbles.size()),
              &nibble_count) == disk_err_none);
  track.nibbles.resize(nibble_count);

  track.sectors.assign(dos_track_size, 0xEE);
  std::vector<uint8_t> scratch(disk_encoding_scratch_size, 0);
  REQUIRE(disk_encoding_denibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), 0,
              track.nibbles.data(), nibble_count, track.sectors.data(),
              scratch.data()) == disk_err_none);
  return track;
}

}  // namespace

TEST_CASE("DiskContainer: a zip archive names the image it carries") {
  const std::string archive = TestFixtures::get_fixture_path("minimal.dsk.zip");
  std::array<char, 256> name{};
  REQUIRE(
      disk_container_payload_name(archive.c_str(), name.data(), name.size()));
  CHECK(std::string(name.data()) == "minimal.dsk");
}

TEST_CASE("DiskContainer: [MB-1] a MacBinary II header is known by its CRC") {
  std::vector<uint8_t> header =
      read_file(TestFixtures::get_fixture_path("minimal-macbinary.dsk"));
  REQUIRE(header.size() == dsk_image_size + macbinary_header_len);
  header.resize(macbinary_header_len);
  REQUIRE(header[122] == 0x81);
  REQUIRE(header[123] == 0x81);
  REQUIRE(header[124] == 0x34);
  REQUIRE(header[125] == 0x6C);
  CHECK(macbinary_crc16(header.data(), macbinary_crc_offset) == 0x346C);

  const auto wrapped_size =
      static_cast<uint32_t>(dsk_image_size + macbinary_header_len);
  CHECK(disk_container_detect_macbinary(header.data(), header.size(),
                                        wrapped_size) == 128);

  SUBCASE("a MacBinary III writer version is accepted once the CRC agrees") {
    header[122] = 0x82;
    CHECK(disk_container_detect_macbinary(header.data(), header.size(),
                                          wrapped_size) == 0);
    const uint16_t crc = macbinary_crc16(header.data(), macbinary_crc_offset);
    header[124] = static_cast<uint8_t>(crc >> 8);
    header[125] = static_cast<uint8_t>(crc & 0xFF);
    CHECK(disk_container_detect_macbinary(header.data(), header.size(),
                                          wrapped_size) == 128);
  }
  SUBCASE("one wrong CRC byte is a refusal") {
    header[125] ^= 0x01;
    CHECK(disk_container_detect_macbinary(header.data(), header.size(),
                                          wrapped_size) == 0);
  }
  SUBCASE("a reader version other than II is a refusal") {
    header[123] = 0x82;
    CHECK(disk_container_detect_macbinary(header.data(), header.size(),
                                          wrapped_size) == 0);
  }
  SUBCASE("a non-zero byte at 74 or 82 is a refusal") {
    header[74] = 1;
    CHECK(disk_container_detect_macbinary(header.data(), header.size(),
                                          wrapped_size) == 0);
    header[74] = 0;
    header[82] = 1;
    CHECK(disk_container_detect_macbinary(header.data(), header.size(),
                                          wrapped_size) == 0);
  }
  SUBCASE("a MacBinary I header, zero at 122 and 123, is not recognised") {
    header[122] = 0;
    header[123] = 0;
    header[124] = 0;
    header[125] = 0;
    CHECK(disk_container_detect_macbinary(header.data(), header.size(),
                                          wrapped_size) == 0);
  }
  SUBCASE("a header that is all the file there is wraps nothing") {
    CHECK(disk_container_detect_macbinary(header.data(), header.size(),
                                          macbinary_header_len) == 0);
  }
}

TEST_CASE(
    "DiskContainer: [MB-2] a MacBinary-wrapped DSK decodes as the bare image") {
  const std::vector<uint8_t> bare =
      read_file(TestFixtures::get_fixture_path("minimal.dsk"));
  REQUIRE(bare.size() == dsk_image_size);

  const OpenedImage_t wrapped(
      TestFixtures::get_fixture_path("minimal-macbinary.dsk"));
  CHECK(std::string(wrapped.driver->name) == "DOS Order");

  const Track_t track = read_track0(wrapped);
  CHECK(track.bit_count == dsk_track_bit_count);
  REQUIRE(track.nibbles.size() > first_data_field_at + 343);
  CHECK(track.nibbles[0] == 0xFF);
  CHECK(memcmp(track.nibbles.data() + first_address_field_at,
               track0_sector0_address_field,
               sizeof(track0_sector0_address_field)) == 0);
  CHECK(track.nibbles[first_data_field_at] == 0xD5);
  CHECK(track.nibbles[first_data_field_at + 1] == 0xAA);
  CHECK(track.nibbles[first_data_field_at + 2] == 0xAD);
  CHECK(track.nibbles[first_data_field_at + 3] == zero_sector_nibble);
  CHECK(track.nibbles[first_data_field_at + 3 + 342] == zero_sector_nibble);
  CHECK(memcmp(track.sectors.data(), bare.data(), dos_track_size) == 0);

  const OpenedImage_t plain(TestFixtures::get_fixture_path("minimal.dsk"));
  const Track_t bare_track = read_track0(plain);
  CHECK(bare_track.bit_count == track.bit_count);
  CHECK(bare_track.nibbles == track.nibbles);
}

TEST_CASE(
    "DiskContainer: [MB-3] an image that merely looks like MacBinary I is "
    "loaded whole") {
  const std::string path =
      TestFixtures::get_fixture_path("minimal-falsepositive.dsk");
  const std::vector<uint8_t> image = read_file(path);
  REQUIRE(image.size() == dsk_image_size);
  REQUIRE(image[0] == 0x00);
  REQUIRE(image[1] == 0x05);
  REQUIRE(image[122] == 0x00);
  CHECK(disk_container_detect_macbinary(image.data(), macbinary_header_len,
                                        static_cast<uint32_t>(image.size())) ==
        0);

  const OpenedImage_t opened(path);
  CHECK(std::string(opened.driver->name) == "DOS Order");
  const Track_t track = read_track0(opened);
  CHECK(track.bit_count == dsk_track_bit_count);
  CHECK(track.sectors[0] == 0x00);
  CHECK(track.sectors[1] == 0x05);
  CHECK(memcmp(track.sectors.data(), image.data(), dos_track_size) == 0);
}
