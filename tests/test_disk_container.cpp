// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/media/image_container/ImageContainer.h"
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
  CHECK(image_container_detect_macbinary(image.data(), macbinary_header_len,
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

TEST_CASE(
    "DiskContainer: [SH-1] the disk_container names answer as the library "
    "does") {
  const std::vector<uint8_t> bare =
      read_file(TestFixtures::get_fixture_path("minimal.dsk"));
  const std::string archive = TestFixtures::get_fixture_path("minimal.dsk.gz");

  std::array<char, 256> name{};
  REQUIRE(
      disk_container_payload_name(archive.c_str(), name.data(), name.size()));
  CHECK(std::string(name.data()) == "minimal.dsk");
  CHECK_FALSE(disk_container_payload_name(nullptr, name.data(), name.size()));

  std::array<char, 512> path{};
  bool is_temporary = false;
  REQUIRE(disk_container_prepare_compressed_path(
      archive.c_str(), path.data(), path.size(),
      disk_container::floppy_decompression_threshold, &is_temporary));
  REQUIRE(is_temporary);
  CHECK(read_file(path.data()) == bare);
  unlink(path.data());

  CHECK_FALSE(disk_container_prepare_compressed_path(
      archive.c_str(), path.data(), path.size(), 0, &is_temporary));
  CHECK_FALSE(is_temporary);

  const std::vector<uint8_t> wrapped =
      read_file(TestFixtures::get_fixture_path("minimal-macbinary.dsk"));
  CHECK(disk_container_detect_macbinary(
            wrapped.data(), macbinary_header_len,
            static_cast<uint32_t>(wrapped.size())) == 128);

  CHECK(disk_container_supported_extensions() ==
        image_container_supported_extensions());
}
