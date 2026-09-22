// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/DoDriver.h"
#include "apple2/peripherals/disk/formats/IieDriver.h"
#include "apple2/peripherals/disk/formats/PoDriver.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr size_t sector_size = 256;
constexpr size_t track_size = sector_size * sectors_per_track;
constexpr uint32_t quarter_tracks_per_cylinder = 4;

auto read_file(const std::string& path) -> std::vector<uint8_t> {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>());
}

auto write_file(const std::string& path, const std::vector<uint8_t>& bytes)
    -> void {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  REQUIRE(out.is_open());
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  REQUIRE(out.good());
}

// A MacBinary-sized run of junk in front of the image, so that any read
// that forgets the offset lands in bytes the fixture never held.
auto prepend_junk(const std::vector<uint8_t>& image, size_t junk_size)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> wrapped(junk_size, 0xC3);
  wrapped.insert(wrapped.end(), image.begin(), image.end());
  return wrapped;
}

struct TrackBits_t {
  std::vector<uint8_t> bits;
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
};

auto read_track(const DiskFormatDriver_t& driver, void* instance,
                uint32_t quarter_track) -> TrackBits_t {
  TrackBits_t track;
  track.bits.assign(max_track_bits / 8, 0);
  REQUIRE(driver.read_track_bits(instance, quarter_track, track.bits.data(),
                                 max_track_bits, &track.bit_count,
                                 &track.bit_timing) == disk_err_none);
  return track;
}

// The sixteen sectors of a synthesised track, in the logical order the
// given table assigns to the address fields.
auto decode_track(const TrackBits_t& track, uint32_t cylinder,
                  DiskSectorOrder_e order) -> std::vector<uint8_t> {
  std::vector<uint8_t> nibbles(nibbles_per_track, 0);
  uint32_t nibble_count = 0;
  REQUIRE(disk_encoding_bits_to_nibbles(track.bits.data(), track.bit_count,
                                        nibbles.data(), nibbles_per_track,
                                        &nibble_count) == disk_err_none);
  std::vector<uint8_t> sectors(track_size, 0);
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  REQUIRE(disk_encoding_denibblize_track(
              disk_encoding_sector_order(order), cylinder, nibbles.data(),
              nibble_count, sectors.data(), scratch.data()) == disk_err_none);
  return sectors;
}

}  // namespace

TEST_CASE("DiskSector: a ProDOS-order image opens through the loader") {
  auto image = TestFixtures::create_ephemeral("minimal.po");
  disk_loader_reset();

  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(image.c_str(), &driver, &instance) == disk_err_none);
  REQUIRE(driver != nullptr);
  REQUIRE(instance != nullptr);
  CHECK(std::string(driver->name) == "ProDOS Order");
  driver->close(instance);
}

TEST_CASE("DiskSector: [IIE-1] a legacy image reads the same behind a prefix") {
  constexpr size_t prefix = 128;
  constexpr uint32_t cylinder = 1;
  auto bare = TestFixtures::create_ephemeral("minimal-legacy.iie");
  auto wrapped = TestFixtures::create_ephemeral("minimal-legacy.iie");
  const std::vector<uint8_t> bare_bytes = read_file(bare.path());
  write_file(wrapped.path(), prepend_junk(bare_bytes, prefix));

  void* bare_instance = nullptr;
  void* wrapped_instance = nullptr;
  REQUIRE(g_iie_driver.open(bare.c_str(), 0, false, &bare_instance) ==
          disk_err_none);
  REQUIRE(g_iie_driver.open(wrapped.c_str(), prefix, false,
                            &wrapped_instance) == disk_err_none);

  const TrackBits_t from_bare =
      read_track(g_iie_driver, bare_instance, cylinder * 4);
  const TrackBits_t from_wrapped =
      read_track(g_iie_driver, wrapped_instance, cylinder * 4);
  CHECK(from_wrapped.bit_count == from_bare.bit_count);
  CHECK(from_wrapped.bits == from_bare.bits);

  // The header map is the DOS 3.3 interleave, so decoding through the DOS
  // table puts file sector s back at logical s: track 1 sector s is 0x10 + s.
  const std::vector<uint8_t> sectors =
      decode_track(from_wrapped, cylinder, disk_sector_order_dos);
  CHECK(sectors[1 * sector_size] == 0x11);
  CHECK(sectors[15 * sector_size] == 0x1F);
  const std::vector<uint8_t> file_track(
      bare_bytes.begin() + 30 + (cylinder * track_size),
      bare_bytes.begin() + 30 + ((cylinder + 1) * track_size));
  CHECK(sectors == file_track);

  g_iie_driver.close(bare_instance);
  g_iie_driver.close(wrapped_instance);
}

TEST_CASE("DiskSector: [IIE-2] a nibble image reads the same behind a prefix") {
  constexpr size_t prefix = 128;
  auto bare = TestFixtures::create_ephemeral("minimal-nibble.iie");
  auto wrapped = TestFixtures::create_ephemeral("minimal-nibble.iie");
  const std::vector<uint8_t> bare_bytes = read_file(bare.path());
  write_file(wrapped.path(), prepend_junk(bare_bytes, prefix));

  void* bare_instance = nullptr;
  void* wrapped_instance = nullptr;
  REQUIRE(g_iie_driver.open(bare.c_str(), 0, false, &bare_instance) ==
          disk_err_none);
  REQUIRE(g_iie_driver.open(wrapped.c_str(), prefix, false,
                            &wrapped_instance) == disk_err_none);

  // Per-track counts of 6,208, 6,656 and 100 nibbles repeat, so track 2 is
  // the short one: 48 gap nibbles and 6 more before the data prologue read
  // as ten-cell self-sync, the other 46 as eight-cell data.
  const TrackBits_t short_track = read_track(g_iie_driver, wrapped_instance,
                                             2 * quarter_tracks_per_cylinder);
  CHECK(short_track.bit_count == (54 * 10) + (46 * 8));
  std::vector<uint8_t> expected(max_track_bits / 8, 0);
  uint32_t expected_count = 0;
  REQUIRE(disk_encoding_nibbles_to_bits(
              &bare_bytes[88 + 6208 + 6656], 100, nullptr, expected.data(),
              max_track_bits, &expected_count) == disk_err_none);
  CHECK(short_track.bit_count == expected_count);
  CHECK(short_track.bits == expected);

  const TrackBits_t full_track = read_track(g_iie_driver, wrapped_instance,
                                            1 * quarter_tracks_per_cylinder);
  const TrackBits_t bare_full =
      read_track(g_iie_driver, bare_instance, 1 * quarter_tracks_per_cylinder);
  CHECK(full_track.bit_count == bare_full.bit_count);
  CHECK(full_track.bits == bare_full.bits);

  g_iie_driver.close(bare_instance);
  g_iie_driver.close(wrapped_instance);
}
