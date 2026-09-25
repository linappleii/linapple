// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/IieDriver.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr size_t sector_size = 256;
constexpr size_t track_size = sector_size * sectors_per_track;

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
auto decode_track_with(const TrackBits_t& track, uint32_t cylinder,
                       const uint8_t* sector_order) -> std::vector<uint8_t> {
  std::vector<uint8_t> nibbles(nibbles_per_track, 0);
  uint32_t nibble_count = 0;
  REQUIRE(disk_encoding_bits_to_nibbles(track.bits.data(), track.bit_count,
                                        nibbles.data(), nibbles_per_track,
                                        &nibble_count) == disk_err_none);
  std::vector<uint8_t> sectors(track_size, 0);
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  REQUIRE(disk_encoding_denibblize_track(sector_order, cylinder, nibbles.data(),
                                         nibble_count, sectors.data(),
                                         scratch.data()) == disk_err_none);
  return sectors;
}

auto decode_track(const TrackBits_t& track, uint32_t cylinder,
                  DiskSectorOrder_e order) -> std::vector<uint8_t> {
  return decode_track_with(track, cylinder, disk_encoding_sector_order(order));
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

auto synthesise_sectors(uint32_t cylinder, DiskSectorOrder_e order,
                        const std::vector<uint8_t>& sectors)
    -> SynthesisedTrack_t {
  REQUIRE(sectors.size() == track_size);
  SynthesisedTrack_t out;
  out.sectors = sectors;
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

auto synthesise_track(uint32_t cylinder, DiskSectorOrder_e order)
    -> SynthesisedTrack_t {
  std::vector<uint8_t> sectors(track_size, 0);
  for (size_t i = 0; i < track_size; ++i) {
    sectors[i] = static_cast<uint8_t>(0xA0 + (i / sector_size));
  }
  return synthesise_sectors(cylinder, order, sectors);
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

TEST_CASE(
    "DiskSector: [IIE-3] a nibble count the track cannot hold is "
    "refused") {
  auto image = TestFixtures::create_ephemeral("minimal-nibble.iie");
  std::vector<uint8_t> bytes = read_file(image.path());

  // Track 1's count sits at 14 + 2 * 1 and is 6,656; one more is 0x1A01.
  // Padding the file keeps the size check out of the verdict.
  bytes[16] = 0x01;
  bytes[17] = 0x1A;
  bytes.insert(bytes.end(), 1024, 0xFF);
  write_file(image.path(), bytes);

  void* instance = nullptr;
  CHECK(g_iie_driver.open(image.c_str(), 0, false, &instance) ==
        disk_err_corrupt);
  CHECK(instance == nullptr);

  bytes[16] = 0x00;
  write_file(image.path(), bytes);
  REQUIRE(g_iie_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);
  CHECK(read_track(g_iie_driver, instance, 1 * 4).bit_count > 0);
  g_iie_driver.close(instance);
}

TEST_CASE("DiskSector: [SEC-E1] a file of the wrong size opens as corrupt") {
  // Not a whole number of sectors, and whole sectors short of one track.
  auto unaligned = TestFixtures::create_ephemeral_blank("odd.dsk", 5000);
  auto short_image = TestFixtures::create_ephemeral_blank("short.po", 3072);

  void* instance = reinterpret_cast<void*>(1);
  CHECK(g_do_driver.open(unaligned.c_str(), 0, false, &instance) ==
        disk_err_corrupt);
  CHECK(instance == nullptr);
  instance = reinterpret_cast<void*>(1);
  CHECK(g_po_driver.open(short_image.c_str(), 0, false, &instance) ==
        disk_err_corrupt);
  CHECK(instance == nullptr);

  // A prefix the file cannot contain is a header the file falls short of.
  instance = reinterpret_cast<void*>(1);
  CHECK(g_do_driver.open(unaligned.c_str(), 6000, false, &instance) ==
        disk_err_corrupt);
  CHECK(instance == nullptr);
}

TEST_CASE("DiskSector: [SEC-E2] a missing file opens as file_not_found") {
  const char* missing = "/nonexistent_dir_12345/missing.dsk";
  void* instance = reinterpret_cast<void*>(1);
  CHECK(g_do_driver.open(missing, 0, false, &instance) ==
        disk_err_file_not_found);
  CHECK(instance == nullptr);
  instance = reinterpret_cast<void*>(1);
  CHECK(g_po_driver.open(missing, 0, true, &instance) ==
        disk_err_file_not_found);
  CHECK(instance == nullptr);
}

namespace {

// The patterned ProDOS image cut or padded to a given size; the padding is a
// byte no sector of the fixture carries, so a read that strays past the
// image's 143,360 bytes shows up in the decoded track.
auto resized_po(size_t size) -> TestFixtures::EphemeralDiskFixture_t {
  auto image = TestFixtures::create_ephemeral("minimal.po");
  std::vector<uint8_t> bytes = read_file(image.path());
  REQUIRE(bytes.size() == 143360);
  bytes.resize(size, 0xC3);
  write_file(image.path(), bytes);
  return image;
}

auto file_track(const std::vector<uint8_t>& file, size_t data_offset,
                uint32_t cylinder) -> std::vector<uint8_t> {
  const size_t begin = data_offset + (cylinder * track_size);
  std::vector<uint8_t> track(track_size, 0);
  const size_t available = file.size() > begin ? file.size() - begin : 0;
  const size_t count = available < track_size ? available : track_size;
  std::copy(file.begin() + static_cast<ptrdiff_t>(begin),
            file.begin() + static_cast<ptrdiff_t>(begin + count),
            track.begin());
  return track;
}

}  // namespace

TEST_CASE(
    "DiskSector: [SEC-C4-1] open admits every size the probe does and no "
    "other") {
  const std::vector<uint8_t> sample =
      read_file(TestFixtures::get_fixture_path("minimal.po"));

  for (const size_t size :
       {143105U, 143200U, 143360U, 143364U, 143403U, 143488U}) {
    CAPTURE(size);
    auto image = resized_po(size);
    CHECK(g_po_driver.probe(sample.data(), sample.size(),
                            static_cast<uint32_t>(size),
                            ".po") != disk_probe_no);
    void* instance = nullptr;
    REQUIRE(g_po_driver.open(image.c_str(), 0, false, &instance) ==
            disk_err_none);
    REQUIRE(instance != nullptr);
    g_po_driver.close(instance);
  }

  // Past the family's ceiling the size alone decides, before any allocation.
  for (const size_t size : {143489U, 163840U}) {
    CAPTURE(size);
    auto image = resized_po(size);
    CHECK(g_po_driver.probe(sample.data(), sample.size(),
                            static_cast<uint32_t>(size),
                            ".po") == disk_probe_no);
    void* instance = reinterpret_cast<void*>(1);
    CHECK(g_po_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_unsupported);
    CHECK(instance == nullptr);
  }

  // Below it, a size the probe would not admit is a damaged image: a whole
  // sector or more short, and a length in the gap between the window and
  // the lone values.
  for (const size_t size : {143000U, 143104U, 143365U, 143402U, 143487U}) {
    CAPTURE(size);
    auto image = resized_po(size);
    CHECK(g_po_driver.probe(sample.data(), sample.size(),
                            static_cast<uint32_t>(size),
                            ".po") == disk_probe_no);
    void* instance = reinterpret_cast<void*>(1);
    CHECK(g_po_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_corrupt);
    CHECK(instance == nullptr);
  }
}

TEST_CASE(
    "DiskSector: [SEC-C4-2] trailing bytes are neither read nor written "
    "back") {
  constexpr size_t trailing = 4;
  constexpr uint32_t last = 34;
  auto image = resized_po(143360 + trailing);
  const std::vector<uint8_t> before = read_file(image.path());

  disk_loader_reset();
  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(image.c_str(), &driver, &instance) == disk_err_none);
  REQUIRE(driver != nullptr);
  CHECK(std::string(driver->name) == "ProDOS Order");

  const TrackBits_t read_back = read_track(*driver, instance, last * 4);
  CHECK(decode_track(read_back, last, disk_sector_order_prodos) ==
        file_track(before, 0, last));
  check_blank_read(*driver, instance, 35 * 4);

  const SynthesisedTrack_t written =
      synthesise_track(last, disk_sector_order_prodos);
  REQUIRE(driver->write_track_bits(instance, last * 4,
                                   written.track.bits.data(),
                                   written.track.bit_count) == disk_err_none);
  driver->close(instance);

  const std::vector<uint8_t> after = read_file(image.path());
  REQUIRE(after.size() == before.size());
  CHECK(file_track(after, 0, last) == written.sectors);
  CHECK(std::vector<uint8_t>(after.end() - trailing, after.end()) ==
        std::vector<uint8_t>(trailing, 0xC3));
  CHECK(std::vector<uint8_t>(after.begin(),
                             after.begin() + (last * track_size)) ==
        std::vector<uint8_t>(before.begin(),
                             before.begin() + (last * track_size)));
}

TEST_CASE(
    "DiskSector: [SEC-C4-3] a file short of its last sector reads what it "
    "has and zero beyond") {
  constexpr size_t short_size = 143105;
  constexpr uint32_t last = 34;
  auto image = resized_po(short_size);
  const std::vector<uint8_t> before = read_file(image.path());

  void* instance = nullptr;
  REQUIRE(g_po_driver.open(image.c_str(), 0, false, &instance) ==
          disk_err_none);

  // 3,841 bytes of track 34 exist: fifteen whole sectors and one byte of the
  // sixteenth, whose marker is 0x2F.
  const std::vector<uint8_t> expected = file_track(before, 0, last);
  REQUIRE(expected[15 * sector_size] == 0x2F);
  REQUIRE(expected[(15 * sector_size) + 1] == 0);
  const TrackBits_t read_back = read_track(g_po_driver, instance, last * 4);
  CHECK(decode_track(read_back, last, disk_sector_order_prodos) == expected);
  CHECK(decode_track(read_track(g_po_driver, instance, 33 * 4), 33,
                     disk_sector_order_prodos) == file_track(before, 0, 33));

  const SynthesisedTrack_t written =
      synthesise_track(last, disk_sector_order_prodos);
  REQUIRE(g_po_driver.write_track_bits(
              instance, last * 4, written.track.bits.data(),
              written.track.bit_count) == disk_err_none);
  g_po_driver.close(instance);
  const std::vector<uint8_t> after = read_file(image.path());
  CHECK(after.size() == 143360);
  CHECK(file_track(after, 0, last) == written.sectors);
}

namespace {

// Sixteen sectors decoded as they lie on the surface: slot p is whatever the
// image put in physical sector p.
auto decode_physical(const TrackBits_t& track, uint32_t cylinder)
    -> std::vector<uint8_t> {
  std::array<uint8_t, sectors_per_track> identity{};
  for (size_t i = 0; i < identity.size(); ++i) {
    identity[i] = static_cast<uint8_t>(i);
  }
  return decode_track_with(track, cylinder, identity.data());
}

auto to_nibbles(const TrackBits_t& track) -> std::vector<uint8_t> {
  std::vector<uint8_t> nibbles(nibbles_per_track, 0);
  uint32_t nibble_count = 0;
  REQUIRE(disk_encoding_bits_to_nibbles(track.bits.data(), track.bit_count,
                                        nibbles.data(), nibbles_per_track,
                                        &nibble_count) == disk_err_none);
  nibbles.resize(nibble_count);
  return nibbles;
}

auto open_iie(const char* path) -> void* {
  disk_loader_reset();
  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(path, &driver, &instance) == disk_err_none);
  REQUIRE(driver == &g_iie_driver);
  REQUIRE(instance != nullptr);
  return instance;
}

}  // namespace

TEST_CASE(
    "DiskSector: [IIE-4] the legacy layout puts each file sector where the "
    "header map says") {
  constexpr size_t legacy_data_at = 30;
  auto image = TestFixtures::create_ephemeral("minimal-legacy.iie");
  const std::vector<uint8_t> bytes = read_file(image.path());
  REQUIRE(bytes.size() == legacy_data_at + (35 * track_size));
  REQUIRE(bytes[13] == 0);

  // The map at 14 names the physical slot of each file sector; the fixture's
  // is the DOS 3.3 logical-to-physical table, so slot 1 holds file sector 7
  // and slot 13 file sector 1 (Beneath Apple DOS ch. 3).
  const std::array<uint8_t, sectors_per_track> map = {
      0x00, 0x0D, 0x0B, 0x09, 0x07, 0x05, 0x03, 0x01,
      0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x0F};
  CHECK(std::vector<uint8_t>(bytes.begin() + 14, bytes.begin() + 30) ==
        std::vector<uint8_t>(map.begin(), map.end()));

  void* instance = open_iie(image.c_str());

  // Track 2's file sector s is 0x20 + s throughout, so the slots that an
  // identity map would fill with 0x21, 0x22 and 0x2D carry the sectors the
  // header map sends there instead.
  const TrackBits_t track = read_track(g_iie_driver, instance, 2 * 4);
  const std::vector<uint8_t> physical = decode_physical(track, 2);
  CHECK(physical[1 * sector_size] == 0x27);
  CHECK(physical[2 * sector_size] == 0x2E);
  CHECK(physical[13 * sector_size] == 0x21);
  CHECK(physical[(13 * sector_size) + 255] == 0x21);
  for (size_t file_sector = 0; file_sector < sectors_per_track; ++file_sector) {
    CAPTURE(file_sector);
    CHECK(physical[map[file_sector] * sector_size] ==
          static_cast<uint8_t>(0x20 + file_sector));
  }

  // Read back through the DOS table the sectors return in file order, so the
  // whole track equals the file's bytes at 30 + 4096t.
  CHECK(decode_track(track, 2, disk_sector_order_dos) ==
        file_track(bytes, legacy_data_at, 2));
  CHECK(decode_track(read_track(g_iie_driver, instance, 34 * 4), 34,
                     disk_sector_order_dos) ==
        file_track(bytes, legacy_data_at, 34));

  g_iie_driver.close(instance);
}

TEST_CASE(
    "DiskSector: [IIE-5] the nibble layout reads each track at the sum of "
    "the counts before it") {
  constexpr size_t header_size = 88;
  constexpr size_t count_of_track_0 = 6208;
  constexpr size_t count_of_track_1 = 6656;
  constexpr size_t count_of_track_2 = 100;
  auto image = TestFixtures::create_ephemeral("minimal-nibble.iie");
  const std::vector<uint8_t> bytes = read_file(image.path());
  REQUIRE(bytes.size() == 155556);
  REQUIRE(bytes[13] == 3);

  // The little-endian count of track t sits at 14 + 2t.
  CHECK(read_u16_le(&bytes[14]) == count_of_track_0);
  CHECK(read_u16_le(&bytes[16]) == count_of_track_1);
  CHECK(read_u16_le(&bytes[18]) == count_of_track_2);
  CHECK(read_u16_le(&bytes[14 + (2 * 34)]) == count_of_track_1);

  void* instance = open_iie(image.c_str());

  // Track 0 is a formatted track of zero sectors: a 48-nibble gap, the first
  // address prologue, and sixteen sectors that decode to nothing.
  const std::vector<uint8_t> track_0 =
      to_nibbles(read_track(g_iie_driver, instance, 0));
  REQUIRE(track_0.size() == count_of_track_0);
  CHECK(std::vector<uint8_t>(track_0.begin(), track_0.begin() + 48) ==
        std::vector<uint8_t>(48, 0xFF));
  CHECK(track_0[48] == 0xD5);
  CHECK(track_0[49] == 0xAA);
  CHECK(track_0[50] == 0x96);
  CHECK(track_0 ==
        std::vector<uint8_t>(bytes.begin() + header_size,
                             bytes.begin() + header_size + count_of_track_0));
  CHECK(decode_track(read_track(g_iie_driver, instance, 0), 0,
                     disk_sector_order_dos) ==
        std::vector<uint8_t>(track_size, 0));

  // Track 1 fills its whole slot; track 2, read after it, is the 100 nibbles
  // that begin where track 1's count ends.
  const size_t track_1_at = header_size + count_of_track_0;
  const size_t track_2_at = track_1_at + count_of_track_1;
  const std::vector<uint8_t> track_1 =
      to_nibbles(read_track(g_iie_driver, instance, 1 * 4));
  REQUIRE(track_1.size() == count_of_track_1);
  CHECK(track_1 ==
        std::vector<uint8_t>(bytes.begin() + track_1_at,
                             bytes.begin() + track_1_at + count_of_track_1));

  const TrackBits_t short_track = read_track(g_iie_driver, instance, 2 * 4);
  CHECK(short_track.bit_count == (54 * 10) + (46 * 8));
  const std::vector<uint8_t> track_2 = to_nibbles(short_track);
  REQUIRE(track_2.size() == count_of_track_2);
  CHECK(track_2 ==
        std::vector<uint8_t>(bytes.begin() + track_2_at,
                             bytes.begin() + track_2_at + count_of_track_2));
  CHECK(track_2[71] == 0x96);

  // Track 3 starts the cycle again, so its offset carries all three counts.
  const size_t track_3_at = track_2_at + count_of_track_2;
  const std::vector<uint8_t> track_3 =
      to_nibbles(read_track(g_iie_driver, instance, 3 * 4));
  REQUIRE(track_3.size() == count_of_track_0);
  CHECK(track_3 ==
        std::vector<uint8_t>(bytes.begin() + track_3_at,
                             bytes.begin() + track_3_at + count_of_track_0));

  g_iie_driver.close(instance);
}

namespace {

// Physical slot p of a synthesised track holds file sector table[p]: the DOS
// 3.3 table from Beneath Apple DOS ch. 3, the ProDOS one from its block map.
// A driver that swapped or mirrored a table lands the pattern in the wrong
// slots, so these are the goldens rather than the encoder's own copies.
constexpr std::array<uint8_t, sectors_per_track> prodos_slots = {
    0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B,
    0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F};
constexpr std::array<uint8_t, sectors_per_track> dos_slots = {
    0x00, 0x07, 0x0E, 0x06, 0x0D, 0x05, 0x0C, 0x04,
    0x0B, 0x03, 0x0A, 0x02, 0x09, 0x01, 0x08, 0x0F};

// Sector s filled with s, so every byte names the sector it belongs to.
auto numbered_sectors() -> std::vector<uint8_t> {
  std::vector<uint8_t> sectors(track_size, 0);
  for (size_t i = 0; i < track_size; ++i) {
    sectors[i] = static_cast<uint8_t>(i / sector_size);
  }
  return sectors;
}

struct SectorWriter_t {
  const DiskFormatDriver_t* driver;
  DiskSectorOrder_e order;
  const std::array<uint8_t, sectors_per_track>* slots;
  const char* fixture;
  const char* extension;
};

const std::array<SectorWriter_t, 2> sector_writers = {
    SectorWriter_t{&g_po_driver, disk_sector_order_prodos, &prodos_slots,
                   "minimal.po", ".po"},
    SectorWriter_t{&g_do_driver, disk_sector_order_dos, &dos_slots,
                   "minimal.dsk", ".do"}};

}  // namespace

TEST_CASE(
    "DiskSector: [SEC-F1-1] the same bytes interleave by the order they are "
    "opened as") {
  constexpr uint32_t cylinder = 1;
  auto po = TestFixtures::create_ephemeral("minimal.po");
  const std::vector<uint8_t> bytes = read_file(po.path());
  auto as_dos = TestFixtures::create_ephemeral_blank("same-bytes.dsk", 0);
  write_file(as_dos.path(), bytes);

  disk_loader_reset();
  const DiskFormatDriver_t* driver = nullptr;
  void* po_instance = nullptr;
  REQUIRE(disk_loader_open(po.c_str(), &driver, &po_instance) == disk_err_none);
  REQUIRE(driver == &g_po_driver);
  void* do_instance = nullptr;
  REQUIRE(g_do_driver.open(as_dos.c_str(), 0, false, &do_instance) ==
          disk_err_none);

  const TrackBits_t po_track =
      read_track(g_po_driver, po_instance, cylinder * 4);
  const TrackBits_t do_track =
      read_track(g_do_driver, do_instance, cylinder * 4);
  const std::vector<uint8_t> via_po = decode_physical(po_track, cylinder);
  const std::vector<uint8_t> via_do = decode_physical(do_track, cylinder);

  // Track 1's file sector s is 0x10 + s, so slot 1 carries sector 8 under
  // ProDOS and sector 7 under DOS.
  CHECK(via_po[1 * sector_size] == 0x18);
  CHECK(via_do[1 * sector_size] == 0x17);
  for (size_t slot = 0; slot < sectors_per_track; ++slot) {
    CAPTURE(slot);
    CHECK(via_po[slot * sector_size] == 0x10 + prodos_slots[slot]);
    CHECK(via_po[(slot * sector_size) + 255] == 0x10 + prodos_slots[slot]);
    CHECK(via_do[slot * sector_size] == 0x10 + dos_slots[slot]);
    CHECK(via_do[(slot * sector_size) + 255] == 0x10 + dos_slots[slot]);
  }

  CHECK(decode_track(po_track, cylinder, disk_sector_order_prodos) ==
        file_track(bytes, 0, cylinder));
  CHECK(decode_track(do_track, cylinder, disk_sector_order_dos) ==
        file_track(bytes, 0, cylinder));

  g_po_driver.close(po_instance);
  g_do_driver.close(do_instance);
}

TEST_CASE(
    "DiskSector: [SEC-F1-2] a numbered track written through each order "
    "lands in file order") {
  constexpr uint32_t cylinder = 3;
  const std::vector<uint8_t> pattern = numbered_sectors();

  for (const SectorWriter_t& writer : sector_writers) {
    INFO("driver := ", std::string(writer.driver->name));
    auto image = TestFixtures::create_ephemeral_blank(
        std::string("fresh") + writer.extension, 0);
    REQUIRE(unlink(image.c_str()) == 0);
    REQUIRE(writer.driver->create(image.c_str()) == disk_err_none);

    void* instance = nullptr;
    REQUIRE(writer.driver->open(image.c_str(), 0, false, &instance) ==
            disk_err_none);
    const SynthesisedTrack_t written =
        synthesise_sectors(cylinder, writer.order, pattern);
    REQUIRE(writer.driver->write_track_bits(
                instance, cylinder * 4, written.track.bits.data(),
                written.track.bit_count) == disk_err_none);
    writer.driver->close(instance);

    // The file keeps sector s at s * 256 whatever the order; only the surface
    // differs between the two.
    const std::vector<uint8_t> after = read_file(image.path());
    REQUIRE(after.size() == 143360);
    CHECK(file_track(after, 0, cylinder) == pattern);
    for (size_t sector = 0; sector < sectors_per_track; ++sector) {
      CAPTURE(sector);
      const size_t at = (cylinder * track_size) + (sector * sector_size);
      CHECK(after[at] == sector);
      CHECK(after[at + 255] == sector);
    }
    CHECK(std::vector<uint8_t>(after.begin(),
                               after.begin() + (cylinder * track_size)) ==
          std::vector<uint8_t>(cylinder * track_size, 0));
    CHECK(
        std::vector<uint8_t>(after.begin() + ((cylinder + 1) * track_size),
                             after.end()) ==
        std::vector<uint8_t>(after.size() - ((cylinder + 1) * track_size), 0));

    REQUIRE(writer.driver->open(image.c_str(), 0, false, &instance) ==
            disk_err_none);
    const TrackBits_t read_back =
        read_track(*writer.driver, instance, cylinder * 4);
    CHECK(decode_track(read_back, cylinder, writer.order) == pattern);
    const std::vector<uint8_t> physical = decode_physical(read_back, cylinder);
    for (size_t slot = 0; slot < sectors_per_track; ++slot) {
      CAPTURE(slot);
      CHECK(physical[slot * sector_size] == (*writer.slots)[slot]);
    }
    writer.driver->close(instance);
  }
}

TEST_CASE(
    "DiskSector: [SEC-F1-3] a read-only open refuses the write and leaves "
    "the file as it was") {
  constexpr uint32_t cylinder = 3;
  const std::vector<uint8_t> pattern = numbered_sectors();

  for (const SectorWriter_t& writer : sector_writers) {
    INFO("driver := ", std::string(writer.driver->name));
    auto image = TestFixtures::create_ephemeral(writer.fixture);
    const std::vector<uint8_t> before = read_file(image.path());

    void* instance = nullptr;
    REQUIRE(writer.driver->open(image.c_str(), 0, true, &instance) ==
            disk_err_none);
    CHECK(writer.driver->is_write_protected(instance));
    const SynthesisedTrack_t written =
        synthesise_sectors(cylinder, writer.order, pattern);
    CHECK(writer.driver->write_track_bits(
              instance, cylinder * 4, written.track.bits.data(),
              written.track.bit_count) == disk_err_write_protected);
    CHECK(decode_track(read_track(*writer.driver, instance, cylinder * 4),
                       cylinder,
                       writer.order) == file_track(before, 0, cylinder));
    writer.driver->close(instance);
    CHECK(read_file(image.path()) == before);

    REQUIRE(writer.driver->open(image.c_str(), 0, false, &instance) ==
            disk_err_none);
    CHECK_FALSE(writer.driver->is_write_protected(instance));
    writer.driver->close(instance);
  }
}

TEST_CASE(
    "DiskSector: [SEC-F1-4] a created image reopens through the loader as "
    "the order its name says") {
  for (const SectorWriter_t& writer : sector_writers) {
    INFO("driver := ", std::string(writer.driver->name));
    auto image = TestFixtures::create_ephemeral_blank(
        std::string("fresh") + writer.extension, 0);
    REQUIRE(unlink(image.c_str()) == 0);
    REQUIRE(writer.driver->create(image.c_str()) == disk_err_none);

    const std::vector<uint8_t> bytes = read_file(image.path());
    REQUIRE(bytes.size() == 143360);
    CHECK(bytes == std::vector<uint8_t>(143360, 0));

    // A blank carries neither catalog nor directory, so the extension alone
    // decides which order claims it.
    const auto size = static_cast<uint32_t>(bytes.size());
    CHECK(writer.driver->probe(bytes.data(), bytes.size(), size,
                               writer.extension) == disk_probe_possible);
    const DiskFormatDriver_t& other =
        writer.driver == &g_po_driver ? g_do_driver : g_po_driver;
    CHECK(other.probe(bytes.data(), bytes.size(), size, writer.extension) ==
          disk_probe_no);

    disk_loader_reset();
    const DiskFormatDriver_t* driver = nullptr;
    void* instance = nullptr;
    REQUIRE(disk_loader_open(image.c_str(), &driver, &instance) ==
            disk_err_none);
    REQUIRE(driver == writer.driver);
    CHECK_FALSE(driver->is_write_protected(instance));

    const TrackBits_t track = read_track(*driver, instance, 0);
    CHECK(track.bit_count == 50464);
    CHECK(track.bit_timing == disk_default_bit_timing);
    CHECK(decode_track(track, 0, writer.order) ==
          std::vector<uint8_t>(track_size, 0));
    driver->close(instance);
  }
}
