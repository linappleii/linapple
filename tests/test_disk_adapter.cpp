// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "doctest.h"

namespace {

constexpr size_t sector_size = 256;
constexpr size_t track_data_size = sectors_per_track * sector_size;
constexpr uint32_t nibbles_in_synthesised_track = 6208;
constexpr uint32_t cells_in_synthesised_track = 50464;

// Gap 1 is 48 self-sync nibbles at ten cells each, so the address prologue
// opens on cell 480 - byte 60 of the packed medium, and the boundary falls
// between bytes because 480 is a multiple of eight.
constexpr uint32_t first_prologue_cell = 480;

// Four self-sync nibbles are forty cells, which is five whole bytes, so the
// packed gap is this five-byte figure twelve times over.
constexpr std::array<uint8_t, 5> self_sync_pattern = {
    {0xFF, 0x3F, 0xCF, 0xF3, 0xFC}};

using SectorImage_t = std::array<uint8_t, track_data_size>;
using NibbleTrack_t = std::array<uint8_t, nibbles_per_track>;
using CellTrack_t = std::array<uint8_t, max_track_bits / 8>;

// Every sector gets a different ramp so a sector landing in the wrong slot,
// or a field decoded one nibble out of step, changes the bytes that come
// back rather than being masked by a uniform fill.
auto make_sector_image() -> SectorImage_t {
  SectorImage_t image{};
  for (size_t sector = 0; sector < sectors_per_track; ++sector) {
    for (size_t offset = 0; offset < sector_size; ++offset) {
      image[(sector * sector_size) + offset] =
          static_cast<uint8_t>((offset * (sector + 1)) + sector);
    }
  }
  return image;
}

struct SynthesisedTrack_t {
  NibbleTrack_t nibbles{};
  NibbleTrack_t sync_mask{};
  uint32_t count = 0;
};

auto synthesise(const SectorImage_t& sectors, uint32_t track)
    -> SynthesisedTrack_t {
  SynthesisedTrack_t out;
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  REQUIRE(disk_encoding_nibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), track,
              sectors.data(), out.nibbles.data(), out.sync_mask.data(),
              &out.count, scratch.data()) == disk_err_none);
  return out;
}

}  // namespace

TEST_CASE("DiskAdapter: [ADAPT-01] A DOS 3.3 track survives a trip as cells") {
  const SectorImage_t original = make_sector_image();
  const SynthesisedTrack_t track = synthesise(original, 17);
  REQUIRE(track.count == nibbles_in_synthesised_track);

  CellTrack_t cells{};
  uint32_t cell_count = 0;
  REQUIRE(disk_encoding_nibbles_to_bits(
              track.nibbles.data(), track.count, track.sync_mask.data(),
              cells.data(), max_track_bits, &cell_count) == disk_err_none);
  CHECK(cell_count == cells_in_synthesised_track);

  NibbleTrack_t recovered{};
  uint32_t recovered_count = 0;
  REQUIRE(disk_encoding_bits_to_nibbles(cells.data(), cell_count,
                                        recovered.data(), nibbles_per_track,
                                        &recovered_count) == disk_err_none);
  CHECK(recovered_count == nibbles_in_synthesised_track);
  CHECK(recovered == track.nibbles);

  SectorImage_t decoded{};
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  REQUIRE(disk_encoding_denibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), 17,
              recovered.data(), recovered_count, decoded.data(),
              scratch.data()) == disk_err_none);
  CHECK(decoded == original);
}

TEST_CASE("DiskAdapter: [ADAPT-02] Gap 1 puts the first prologue on cell 480") {
  const SynthesisedTrack_t track = synthesise(make_sector_image(), 0);

  CellTrack_t cells{};
  uint32_t cell_count = 0;
  REQUIRE(disk_encoding_nibbles_to_bits(
              track.nibbles.data(), track.count, track.sync_mask.data(),
              cells.data(), max_track_bits, &cell_count) == disk_err_none);

  for (uint32_t byte = 0; byte < first_prologue_cell / 8; ++byte) {
    CHECK(cells[byte] == self_sync_pattern[byte % self_sync_pattern.size()]);
  }

  // Prologue nibbles are data, so their cells are packed eight to a byte and
  // the three of them read straight out of the medium.
  CHECK(cells[first_prologue_cell / 8] == 0xD5);
  CHECK(cells[(first_prologue_cell / 8) + 1] == 0xAA);
  CHECK(cells[(first_prologue_cell / 8) + 2] == 0x96);
}

TEST_CASE("DiskAdapter: [ADAPT-03] A short nibble buffer stops, not overruns") {
  const std::array<uint8_t, 4> nibbles = {{0xD5, 0xAA, 0x96, 0xDE}};
  std::array<uint8_t, 8> cells{};
  uint32_t cell_count = 0;
  REQUIRE(disk_encoding_nibbles_to_bits(nibbles.data(), nibbles.size(), nullptr,
                                        cells.data(), 64,
                                        &cell_count) == disk_err_none);
  CHECK(cell_count == 32);

  std::array<uint8_t, 4> recovered{};
  recovered.fill(0x11);
  uint32_t recovered_count = 0;
  CHECK(disk_encoding_bits_to_nibbles(cells.data(), cell_count,
                                      recovered.data(), 2, &recovered_count) ==
        disk_err_unsupported);
  CHECK(recovered_count == 2);
  CHECK(recovered[0] == 0xD5);
  CHECK(recovered[1] == 0xAA);
  CHECK(recovered[2] == 0x11);
  CHECK(recovered[3] == 0x11);
}

TEST_CASE("DiskAdapter: [ADAPT-04] The adapter refuses a missing buffer") {
  const std::array<uint8_t, 2> nibbles = {{0xD5, 0xAA}};
  std::array<uint8_t, 8> cells{};
  uint32_t count = 0x5A5A;

  CHECK(disk_encoding_nibbles_to_bits(nullptr, nibbles.size(), nullptr,
                                      cells.data(), 64,
                                      &count) == disk_err_invalid_argument);
  CHECK(count == 0x5A5A);
  CHECK(disk_encoding_nibbles_to_bits(nibbles.data(), nibbles.size(), nullptr,
                                      nullptr, 64,
                                      &count) == disk_err_invalid_argument);
  CHECK(disk_encoding_nibbles_to_bits(nibbles.data(), nibbles.size(), nullptr,
                                      cells.data(), 64,
                                      nullptr) == disk_err_invalid_argument);

  CHECK(disk_encoding_bits_to_nibbles(nullptr, 16, cells.data(), 2, &count) ==
        disk_err_invalid_argument);
  CHECK(count == 0x5A5A);
  CHECK(disk_encoding_bits_to_nibbles(cells.data(), 16, nullptr, 2, &count) ==
        disk_err_invalid_argument);
  CHECK(disk_encoding_bits_to_nibbles(cells.data(), 16, cells.data(), 2,
                                      nullptr) == disk_err_invalid_argument);
}

TEST_CASE("DiskAdapter: [ADAPT-05] A gap across the index hole is still sync") {
  std::array<uint8_t, 8> cells{};
  uint32_t cell_count = 0;

  // The medium is a loop, so the two 0xFF at the end of this track run into
  // the 0xD5 at its start: three data nibbles at eight cells and two
  // self-sync ones at ten.
  const std::array<uint8_t, 5> wraps_onto_a_prologue = {
      {0xD5, 0xAA, 0x96, 0xFF, 0xFF}};
  REQUIRE(disk_encoding_nibbles_to_bits(
              wraps_onto_a_prologue.data(), wraps_onto_a_prologue.size(),
              nullptr, cells.data(), 64, &cell_count) == disk_err_none);
  CHECK(cell_count == 44);

  // The same run with no prologue waiting for it is five data nibbles.
  const std::array<uint8_t, 5> wraps_onto_data = {
      {0xAA, 0xAA, 0x96, 0xFF, 0xFF}};
  REQUIRE(disk_encoding_nibbles_to_bits(
              wraps_onto_data.data(), wraps_onto_data.size(), nullptr,
              cells.data(), 64, &cell_count) == disk_err_none);
  CHECK(cell_count == 40);
}
