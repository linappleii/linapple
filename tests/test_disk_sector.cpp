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

TEST_CASE("DiskSector: [PO-1] a ProDOS-order volume probes definite") {
  auto image = TestFixtures::create_ephemeral("minimal.po");
  const std::vector<uint8_t> bytes = read_file(image.path());
  const auto size = static_cast<uint32_t>(bytes.size());

  CHECK(g_po_driver.probe(bytes.data(), bytes.size(), size, ".po") ==
        disk_probe_definite);
  CHECK(g_po_driver.probe(bytes.data(), bytes.size(), size, "") ==
        disk_probe_definite);
  CHECK(g_do_driver.probe(bytes.data(), bytes.size(), size, ".po") ==
        disk_probe_no);
}

TEST_CASE("DiskSector: [PO-2] a DOS 3.3 volume still probes definite as DOS") {
  auto image = TestFixtures::create_ephemeral("Master.dsk");
  const std::vector<uint8_t> bytes = read_file(image.path());
  const auto size = static_cast<uint32_t>(bytes.size());

  CHECK(g_do_driver.probe(bytes.data(), bytes.size(), size, ".dsk") ==
        disk_probe_definite);
  CHECK(g_po_driver.probe(bytes.data(), bytes.size(), size, ".dsk") ==
        disk_probe_no);

  disk_loader_reset();
  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(image.c_str(), &driver, &instance) == disk_err_none);
  REQUIRE(driver != nullptr);
  CHECK(std::string(driver->name) == "DOS Order");
  driver->close(instance);
}

namespace {

// A ProDOS volume imaged in DOS order, shaped like the repository's ProDOS
// 2.4.2 disk: a two-block directory with block 2 at DOS sector $B (prev 0,
// next 3) and block 3 at DOS sector $9 (prev 2, next 0).
auto dos_order_prodos_image() -> std::vector<uint8_t> {
  std::vector<uint8_t> image(143360, 0);
  image[0xB00] = 0;
  image[0xB01] = 0;
  image[0xB02] = 3;
  image[0xB03] = 0;
  image[0x900] = 2;
  image[0x901] = 0;
  image[0x902] = 0;
  image[0x903] = 0;
  return image;
}

}  // namespace

TEST_CASE("DiskSector: [DO-2] a ProDOS volume in DOS order probes definite") {
  std::vector<uint8_t> image = dos_order_prodos_image();
  const auto size = static_cast<uint32_t>(image.size());

  CHECK(g_do_driver.probe(image.data(), image.size(), size, ".dsk") ==
        disk_probe_definite);
  CHECK(g_po_driver.probe(image.data(), image.size(), size, "") ==
        disk_probe_possible);

  // A second block whose back link names the wrong predecessor breaks the
  // chain, and the order falls back to a guess.
  image[0x900] = 5;
  CHECK(g_do_driver.probe(image.data(), image.size(), size, ".dsk") ==
        disk_probe_possible);
  image[0x900] = 2;

  // A key block with no successor is not a directory either.
  image[0xB02] = 0;
  CHECK(g_do_driver.probe(image.data(), image.size(), size, ".dsk") ==
        disk_probe_possible);
  image[0xB02] = 3;

  auto file = TestFixtures::create_ephemeral_blank("prodos.dsk", 0);
  write_file(file.path(), image);
  disk_loader_reset();
  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(file.c_str(), &driver, &instance) == disk_err_none);
  REQUIRE(driver != nullptr);
  CHECK(std::string(driver->name) == "DOS Order");
  driver->close(instance);
}

TEST_CASE(
    "DiskSector: [PO-3] a ProDOS-order volume named .dsk still probes "
    "ProDOS") {
  auto source = TestFixtures::create_ephemeral("minimal.po");
  const std::vector<uint8_t> bytes = read_file(source.path());
  const auto size = static_cast<uint32_t>(bytes.size());
  auto renamed = TestFixtures::create_ephemeral_blank("renamed.dsk", 0);
  write_file(renamed.path(), bytes);

  CHECK(g_po_driver.probe(bytes.data(), bytes.size(), size, ".dsk") ==
        disk_probe_definite);
  CHECK(g_do_driver.probe(bytes.data(), bytes.size(), size, ".dsk") ==
        disk_probe_possible);

  disk_loader_reset();
  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(renamed.c_str(), &driver, &instance) ==
          disk_err_none);
  REQUIRE(driver != nullptr);
  CHECK(std::string(driver->name) == "ProDOS Order");

  // Track 1 physical sector 1 carries file sector 8 in ProDOS order, so the
  // pattern byte 0x18 says the interleave came from the ProDOS table.
  const TrackBits_t track = read_track(*driver, instance, 1 * 4);
  const std::vector<uint8_t> sectors =
      decode_track(track, 1, disk_sector_order_prodos);
  CHECK(sectors[8 * sector_size] == 0x18);
  driver->close(instance);
}

TEST_CASE(
    "DiskSector: [PO-4] a DOS 3.3 catalog in ProDOS order probes "
    "definite") {
  std::vector<uint8_t> image(143360, 0);
  // Catalog sector s links to s - 1; in a ProDOS-order file DOS sector s
  // sits at file sector 15 - s for s = 1..14 and at 15 for s = 15.
  for (int sector = 1; sector <= 14; ++sector) {
    image[0x11000 + ((15 - sector) * 0x100) + 2] =
        static_cast<uint8_t>(sector - 1);
  }
  image[0x11000 + (15 * 0x100) + 2] = 14;
  const auto size = static_cast<uint32_t>(image.size());

  CHECK(g_po_driver.probe(image.data(), image.size(), size, ".po") ==
        disk_probe_definite);
  CHECK(g_do_driver.probe(image.data(), image.size(), size, "") ==
        disk_probe_possible);
}

namespace {

// Sixteen patterned sectors of one cylinder, laid down as cells the way the
// image would synthesise them, so a write-back has a track to land.
struct SynthesisedTrack_t {
  std::vector<uint8_t> sectors;
  std::vector<uint8_t> nibbles;
  TrackBits_t track;
};

auto synthesise_track(uint32_t cylinder, DiskSectorOrder_e order)
    -> SynthesisedTrack_t {
  SynthesisedTrack_t out;
  out.sectors.assign(track_size, 0);
  for (size_t i = 0; i < track_size; ++i) {
    out.sectors[i] = static_cast<uint8_t>(0xA0 + (i / sector_size));
  }
  out.nibbles.assign(nibbles_per_track, 0);
  std::vector<uint8_t> sync_mask(nibbles_per_track, 0);
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  uint32_t nibble_count = 0;
  REQUIRE(disk_encoding_nibblize_track(
              disk_encoding_sector_order(order), cylinder, out.sectors.data(),
              out.nibbles.data(), sync_mask.data(), &nibble_count,
              scratch.data()) == disk_err_none);
  out.nibbles.resize(nibble_count);
  out.track.bits.assign(max_track_bits / 8, 0);
  REQUIRE(disk_encoding_nibbles_to_bits(out.nibbles.data(), nibble_count,
                                        sync_mask.data(), out.track.bits.data(),
                                        max_track_bits,
                                        &out.track.bit_count) == disk_err_none);
  return out;
}

auto check_blank_read(const DiskFormatDriver_t& driver, void* instance,
                      uint32_t quarter_track) -> void {
  std::vector<uint8_t> bits(max_track_bits / 8, 0xEE);
  uint32_t bit_count = 123;
  uint8_t bit_timing = 0;
  CHECK(driver.read_track_bits(instance, quarter_track, bits.data(),
                               max_track_bits, &bit_count,
                               &bit_timing) == disk_err_none);
  CHECK(bit_count == 0);
  CHECK(bit_timing == disk_default_bit_timing);
}

}  // namespace

TEST_CASE(
    "DiskSector: [SEC-B1] a write past the last track is dropped and "
    "one on it lands") {
  auto image = TestFixtures::create_ephemeral("minimal.dsk");
  const std::vector<uint8_t> before = read_file(image.path());
  REQUIRE(before.size() == 143360);

  void* instance = nullptr;
  REQUIRE(g_do_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  const SynthesisedTrack_t beyond = synthesise_track(35, disk_sector_order_dos);
  CHECK(g_do_driver.write_track_bits(instance, 35 * 4, beyond.track.bits.data(),
                                     beyond.track.bit_count) == disk_err_none);
  CHECK(read_file(image.path()) == before);

  const SynthesisedTrack_t last = synthesise_track(34, disk_sector_order_dos);
  CHECK(g_do_driver.write_track_bits(instance, 34 * 4, last.track.bits.data(),
                                     last.track.bit_count) == disk_err_none);
  g_do_driver.close(instance);

  const std::vector<uint8_t> after = read_file(image.path());
  REQUIRE(after.size() == 143360);
  // DOS order keeps file sector s at logical s, so the track lands verbatim.
  CHECK(std::vector<uint8_t>(after.begin() + (34 * track_size), after.end()) ==
        last.sectors);
  CHECK(after[34 * track_size] == 0xA0);
  CHECK(after[(34 * track_size) + (15 * sector_size)] == 0xAF);

  REQUIRE(g_do_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);
  const TrackBits_t read_back = read_track(g_do_driver, instance, 34 * 4);
  CHECK(decode_track(read_back, 34, disk_sector_order_dos) == last.sectors);
  g_do_driver.close(instance);
}

TEST_CASE("DiskSector: [SEC-B2] reads past the last track are blank surface") {
  auto dsk = TestFixtures::create_ephemeral("minimal.dsk");
  auto legacy = TestFixtures::create_ephemeral("minimal-legacy.iie");
  auto nibble = TestFixtures::create_ephemeral("minimal-nibble.iie");

  void* instance = nullptr;
  REQUIRE(g_do_driver.open(dsk.c_str(), 0, false, &instance) == disk_err_none);
  check_blank_read(g_do_driver, instance, 35 * 4);
  check_blank_read(g_do_driver, instance, 39 * 4);
  check_blank_read(g_do_driver, instance, UINT32_MAX);
  g_do_driver.close(instance);

  REQUIRE(g_iie_driver.open(legacy.c_str(), 0, false, &instance) ==
          disk_err_none);
  check_blank_read(g_iie_driver, instance, 35 * 4);
  g_iie_driver.close(instance);

  REQUIRE(g_iie_driver.open(nibble.c_str(), 0, false, &instance) ==
          disk_err_none);
  check_blank_read(g_iie_driver, instance, 35 * 4);
  g_iie_driver.close(instance);
}
