// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "doctest.h"

namespace {

constexpr size_t sector_size = 256;
constexpr size_t track_data_size = sectors_per_track * sector_size;  // 4096
constexpr uint32_t expected_nibble_count = 6208;

constexpr uint8_t gcr_high_bit_mask = 0x80;
constexpr uint8_t sync_byte = 0xFF;
constexpr uint8_t addr_4and4_mask = 0x55;
constexpr uint8_t gcr_sync_bit_mask = 0xAA;

auto encode_4and4_high(uint8_t a) -> uint8_t {
  return static_cast<uint8_t>((((a) >> 1U) & addr_4and4_mask) |
                              gcr_sync_bit_mask);
}

auto encode_4and4_low(uint8_t a) -> uint8_t {
  return static_cast<uint8_t>(((a)&addr_4and4_mask) | gcr_sync_bit_mask);
}

auto populate_test_track(std::array<uint8_t, track_data_size>& track) -> void {
  // Sector 0: Edge case - all zeros
  std::fill_n(&track[0 * sector_size], sector_size, static_cast<uint8_t>(0x00));

  // Sector 1: Edge case - all ones
  std::fill_n(&track[1 * sector_size], sector_size, static_cast<uint8_t>(0xFF));

  // Sector 2: Ascending sequence 0x00..0xFF
  for (size_t i = 0; i < sector_size; ++i) {
    track[(2 * sector_size) + i] = static_cast<uint8_t>(i);
  }

  // Sector 3: Descending sequence 0xFF..0x00
  for (size_t i = 0; i < sector_size; ++i) {
    track[(3 * sector_size) + i] = static_cast<uint8_t>(255U - i);
  }

  // Sector 4: Alternating bit pattern 0xAA (10101010)
  std::fill_n(&track[4 * sector_size], sector_size, static_cast<uint8_t>(0xAA));

  // Sector 5: Alternating bit pattern 0x55 (01010101)
  std::fill_n(&track[5 * sector_size], sector_size, static_cast<uint8_t>(0x55));

  // Sector 6: Alternating high/low nibbles without conditional branching
  for (size_t i = 0; i < sector_size; ++i) {
    track[(6 * sector_size) + i] =
        static_cast<uint8_t>(0x0FU ^ ((i & 1U) * 0xFFU));
  }

  // Sector 7: Walking ones
  for (size_t i = 0; i < sector_size; ++i) {
    track[(7 * sector_size) + i] = static_cast<uint8_t>(1U << (i & 7U));
  }

  // Sector 8: Walking zeros
  for (size_t i = 0; i < sector_size; ++i) {
    track[(8 * sector_size) + i] = static_cast<uint8_t>(~(1U << (i & 7U)));
  }

  // Sector 9: Deterministic pseudo-random bytes via linear congruential
  // generator (seed 1)
  uint32_t state1 = 0x12345678U;
  for (size_t i = 0; i < sector_size; ++i) {
    state1 = state1 * 1103515245U + 12345U;
    track[(9 * sector_size) + i] =
        static_cast<uint8_t>((state1 >> 16U) & 0xFFU);
  }

  // Sector 10: 6-bit boundary patterns
  const std::array<uint8_t, 8> boundary_pattern = {
      {0x00, 0x3F, 0x40, 0x7F, 0x80, 0xBF, 0xC0, 0xFF}};
  for (size_t i = 0; i < sector_size; ++i) {
    track[(10 * sector_size) + i] =
        boundary_pattern[i % boundary_pattern.size()];
  }

  // Sector 11: Periodic stride pattern
  for (size_t i = 0; i < sector_size; ++i) {
    track[(11 * sector_size) + i] = static_cast<uint8_t>((i * 17U) & 0xFFU);
  }

  // Sector 12: Linear prime distribution pattern
  for (size_t i = 0; i < sector_size; ++i) {
    track[(12 * sector_size) + i] =
        static_cast<uint8_t>((i * 73U + 0x37U) & 0xFFU);
  }

  // Sector 13: Deterministic pseudo-random bytes via linear congruential
  // generator (seed 2)
  uint32_t state2 = 0x9ABCDEF0U;
  for (size_t i = 0; i < sector_size; ++i) {
    state2 = state2 * 1103515245U + 12345U;
    track[(13 * sector_size) + i] =
        static_cast<uint8_t>((state2 >> 16U) & 0xFFU);
  }

  // Sector 14: Simulated ASCII text range
  for (size_t i = 0; i < sector_size; ++i) {
    track[(14 * sector_size) + i] = static_cast<uint8_t>(0xA0U + (i % 64U));
  }

  // Sector 15: Bitwise NOT of ascending sequence
  for (size_t i = 0; i < sector_size; ++i) {
    track[(15 * sector_size) + i] = static_cast<uint8_t>(0xFFU ^ i);
  }
}

struct NibblizedTrack_t {
  std::array<uint8_t, nibbles_per_track> nibbles{};
  std::array<uint8_t, nibbles_per_track> sync_mask{};
  uint32_t count = 0;
};

auto nibblize(const std::array<uint8_t, track_data_size>& sectors,
              const uint8_t* order, uint32_t track) -> NibblizedTrack_t {
  NibblizedTrack_t out;
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  REQUIRE(disk_encoding_nibblize_track(order, track, sectors.data(),
                                       out.nibbles.data(), out.sync_mask.data(),
                                       &out.count,
                                       scratch.data()) == disk_err_none);
  return out;
}

auto execute_round_trip(int track_num, bool is_dos_order) -> void {
  std::array<uint8_t, track_data_size> original_track{};
  populate_test_track(original_track);

  const NibblizedTrack_t track = nibblize(
      original_track,
      disk_encoding_sector_order(is_dos_order ? disk_sector_order_dos
                                              : disk_sector_order_prodos),
      static_cast<uint32_t>(track_num));
  CHECK(track.count == expected_nibble_count);

  std::array<uint8_t, track_data_size> sectors{};
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  CHECK(disk_encoding_denibblize_track(
            disk_encoding_sector_order(is_dos_order ? disk_sector_order_dos
                                                    : disk_sector_order_prodos),
            static_cast<uint32_t>(track_num), track.nibbles.data(), track.count,
            sectors.data(), scratch.data()) == disk_err_none);

  for (size_t i = 0; i < track_data_size; ++i) {
    CHECK(sectors[i] == original_track[i]);
  }
}

}  // namespace

TEST_CASE(
    "DiskGCR: [GCR-01] disk_encoding_scratch_size ABI Constant "
    "Validation") {
  CHECK(disk_encoding_scratch_size == 0x1800);
}

TEST_CASE("DiskGCR: [GCR-02] DOS 3.3 Sector Order Bit-for-Bit Round-Trip") {
  SUBCASE("Track 0 (Boot track)") { execute_round_trip(0, true); }

  SUBCASE("Track 1 (Standard DOS track)") { execute_round_trip(1, true); }

  SUBCASE("Track 17 (DOS 3.3 VTOC and Catalog track)") {
    execute_round_trip(17, true);
  }

  SUBCASE("Track 34 (Boundary track)") { execute_round_trip(34, true); }
}

TEST_CASE("DiskGCR: [GCR-03] ProDOS Sector Order Bit-for-Bit Round-Trip") {
  SUBCASE("Track 0 (ProDOS boot track)") { execute_round_trip(0, false); }

  SUBCASE("Track 2 (ProDOS volume directory block track)") {
    execute_round_trip(2, false);
  }

  SUBCASE("Track 34 (ProDOS boundary track)") { execute_round_trip(34, false); }
}

TEST_CASE("DiskGCR: [GCR-04] Custom Sector Order Round-Trip") {
  SUBCASE("Explicit DOS 3.3 sector order round-trip") {
    const std::array<uint8_t, sectors_per_track> custom_dos_order = {
        {0x00, 0x07, 0x0E, 0x06, 0x0D, 0x05, 0x0C, 0x04, 0x0B, 0x03, 0x0A, 0x02,
         0x09, 0x01, 0x08, 0x0F}};

    std::array<uint8_t, track_data_size> original_track{};
    populate_test_track(original_track);

    const NibblizedTrack_t track =
        nibblize(original_track, custom_dos_order.data(), 1);
    CHECK(track.count == expected_nibble_count);

    std::array<uint8_t, track_data_size> sectors{};
    std::array<uint8_t, disk_encoding_scratch_size> scratch{};
    CHECK(disk_encoding_denibblize_track(
              custom_dos_order.data(), 1, track.nibbles.data(), track.count,
              sectors.data(), scratch.data()) == disk_err_none);

    for (size_t i = 0; i < track_data_size; ++i) {
      CHECK(sectors[i] == original_track[i]);
    }
  }

  SUBCASE("Explicit ProDOS sector order round-trip") {
    const std::array<uint8_t, sectors_per_track> custom_prodos_order = {
        {0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D,
         0x06, 0x0E, 0x07, 0x0F}};

    std::array<uint8_t, track_data_size> original_track{};
    populate_test_track(original_track);

    const NibblizedTrack_t track =
        nibblize(original_track, custom_prodos_order.data(), 2);
    CHECK(track.count == expected_nibble_count);

    std::array<uint8_t, track_data_size> sectors{};
    std::array<uint8_t, disk_encoding_scratch_size> scratch{};
    CHECK(disk_encoding_denibblize_track(
              custom_prodos_order.data(), 2, track.nibbles.data(), track.count,
              sectors.data(), scratch.data()) == disk_err_none);

    for (size_t i = 0; i < track_data_size; ++i) {
      CHECK(sectors[i] == original_track[i]);
    }
  }

  SUBCASE("Custom inverted sector order maps deterministically") {
    const std::array<uint8_t, sectors_per_track> custom_rev_order = {
        {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}};
    const std::array<uint8_t, sectors_per_track> standard_dos_order = {
        {0x00, 0x07, 0x0E, 0x06, 0x0D, 0x05, 0x0C, 0x04, 0x0B, 0x03, 0x0A, 0x02,
         0x09, 0x01, 0x08, 0x0F}};

    std::array<uint8_t, track_data_size> original_track{};
    populate_test_track(original_track);

    const NibblizedTrack_t track =
        nibblize(original_track, custom_rev_order.data(), 5);
    CHECK(track.count == expected_nibble_count);

    std::array<uint8_t, track_data_size> sectors{};
    std::array<uint8_t, disk_encoding_scratch_size> scratch{};
    CHECK(disk_encoding_denibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), 5,
              track.nibbles.data(), track.count, sectors.data(),
              scratch.data()) == disk_err_none);

    for (size_t sec = 0; sec < sectors_per_track; ++sec) {
      const size_t original_base = custom_rev_order[sec] * sector_size;
      const size_t decoded_base = standard_dos_order[sec] * sector_size;
      for (size_t b = 0; b < sector_size; ++b) {
        CHECK(sectors[decoded_base + b] == original_track[original_base + b]);
      }
    }
  }
}

TEST_CASE(
    "DiskGCR: [GCR-05] Physical GCR 6-and-2 Bit Invariants Across Encoded "
    "Track") {
  std::array<uint8_t, track_data_size> original_track{};
  populate_test_track(original_track);

  const NibblizedTrack_t track = nibblize(
      original_track, disk_encoding_sector_order(disk_sector_order_dos), 0);

  CHECK(track.count == expected_nibble_count);

  // Invariant 1: Physical disk nibbles must always have bit 7 (MSB) set
  for (uint32_t i = 0; i < track.count; ++i) {
    CHECK((track.nibbles[i] & gcr_high_bit_mask) != 0U);
  }

  // Invariant 2: gap 1 opens the track with 48 sync bytes, and every byte the
  // mask calls sync is one of the 0xFF the gaps are made of.
  constexpr uint32_t gap1_size = 48;
  constexpr uint32_t gap2_size = 6;
  constexpr uint32_t gap3_size = 16;
  constexpr uint32_t expected_sync_count =
      gap1_size + (sectors_per_track * (gap2_size + gap3_size));

  uint32_t sync_count = 0;
  for (uint32_t i = 0; i < track.count; ++i) {
    if (track.sync_mask[i] == 0) {
      continue;
    }
    CHECK(track.nibbles[i] == sync_byte);
    ++sync_count;
  }
  CHECK(sync_count == expected_sync_count);

  for (uint32_t i = 0; i < gap1_size; ++i) {
    CHECK(track.sync_mask[i] == 1);
  }
  CHECK(track.sync_mask[gap1_size] == 0);
  CHECK(track.nibbles[gap1_size] == 0xD5);

  // Invariant 3: the cells those nibbles make are the nominal revolution.
  constexpr uint32_t nominal_track_bits = 50464;
  std::array<uint8_t, max_track_bits / 8> bits{};
  uint32_t bit_count = 0;
  REQUIRE(disk_encoding_nibbles_to_bits(
              track.nibbles.data(), track.count, track.sync_mask.data(),
              bits.data(), max_track_bits, &bit_count) == disk_err_none);
  CHECK(bit_count == nominal_track_bits);
}

TEST_CASE(
    "DiskGCR: [GCR-07] Corrupted and Malformed Nibble Streams Robustness") {
  std::array<uint8_t, track_data_size> sectors{};
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  std::array<uint8_t, nibbles_per_track> track_image{};

  SUBCASE("Unformatted track (all 0xFF sync bytes) decodes nothing") {
    track_image.fill(sync_byte);
    sectors.fill(0xAA);

    CHECK(disk_encoding_denibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), 0,
              track_image.data(), nibbles_per_track, sectors.data(),
              scratch.data()) == disk_err_corrupt);

    for (size_t i = 0; i < track_data_size; ++i) {
      CHECK(sectors[i] == 0xAA);
    }
  }

  SUBCASE("Zero-filled track (all 0x00) decodes nothing without crash") {
    track_image.fill(0x00);
    sectors.fill(0x55);

    CHECK(disk_encoding_denibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), 0,
              track_image.data(), nibbles_per_track, sectors.data(),
              scratch.data()) == disk_err_corrupt);

    for (size_t i = 0; i < track_data_size; ++i) {
      CHECK(sectors[i] == 0x55);
    }
  }

  SUBCASE("Truncated nibble stream counts do not overrun buffers") {
    const std::array<uint32_t, 4> truncated_lengths = {{100, 256, 512, 1024}};

    for (size_t i = 0; i < truncated_lengths.size(); ++i) {
      sectors.fill(0x33);
      track_image.fill(sync_byte);

      CHECK(disk_encoding_denibblize_track(
                disk_encoding_sector_order(disk_sector_order_dos), 0,
                track_image.data(), truncated_lengths[i], sectors.data(),
                scratch.data()) == disk_err_corrupt);

      for (size_t b = 0; b < track_data_size; ++b) {
        CHECK(sectors[b] == 0x33);
      }
    }
  }

  SUBCASE("Out-of-range sector number in address field safely ignored") {
    std::array<uint8_t, track_data_size> original_track{};
    populate_test_track(original_track);

    NibblizedTrack_t track = nibblize(
        original_track, disk_encoding_sector_order(disk_sector_order_dos), 0);

    // Sector 0 address field opens after gap 1:
    // [+0..2]: D5 AA 96
    // [+3..4]: Volume (FE)
    // [+5..6]: Track (00)
    // [+7..8]: Sector 4-and-4
    // Inject invalid sector number 25 (valid sectors are 0..15)
    constexpr size_t first_address_field = 48;
    track.nibbles[first_address_field + 7] = encode_4and4_high(25);
    track.nibbles[first_address_field + 8] = encode_4and4_low(25);

    sectors.fill(0x00);
    CHECK(disk_encoding_denibblize_track(
              disk_encoding_sector_order(disk_sector_order_dos), 0,
              track.nibbles.data(), track.count, sectors.data(),
              scratch.data()) == disk_err_corrupt);

    // One sector short is a track the image never sees.
    for (size_t b = 0; b < track_data_size; ++b) {
      CHECK(sectors[b] == 0x00);
    }
  }
}
TEST_CASE("DiskGCR: [ADP-01] A self-sync byte costs ten cells, a data byte 8") {
  const std::array<uint8_t, 3> nibbles = {{0xD5, 0xFF, 0xAA}};
  std::array<uint8_t, 16> bits{};
  uint32_t bit_count = 0;

  const std::array<uint8_t, 3> with_sync = {{0, 1, 0}};
  REQUIRE(disk_encoding_nibbles_to_bits(nibbles.data(), nibbles.size(),
                                        with_sync.data(), bits.data(), 128,
                                        &bit_count) == disk_err_none);
  CHECK(bit_count == 26);

  const std::array<uint8_t, 3> all_data = {{0, 0, 0}};
  REQUIRE(disk_encoding_nibbles_to_bits(nibbles.data(), nibbles.size(),
                                        all_data.data(), bits.data(), 128,
                                        &bit_count) == disk_err_none);
  CHECK(bit_count == 24);
}

TEST_CASE("DiskGCR: [ADP-02] A nibble image's 0xFF run is sync before D5") {
  std::array<uint8_t, 16> bits{};
  uint32_t bit_count = 0;
  std::array<uint8_t, 8> recovered{};
  uint32_t recovered_count = 0;

  const std::array<uint8_t, 5> gap_then_prologue = {
      {0xFF, 0xFF, 0xFF, 0xD5, 0x96}};
  REQUIRE(disk_encoding_nibbles_to_bits(
              gap_then_prologue.data(), gap_then_prologue.size(), nullptr,
              bits.data(), 128, &bit_count) == disk_err_none);
  CHECK(bit_count == (3 * 10) + (2 * 8));
  REQUIRE(disk_encoding_bits_to_nibbles(bits.data(), bit_count,
                                        recovered.data(), recovered.size(),
                                        &recovered_count) == disk_err_none);
  CHECK(recovered_count == gap_then_prologue.size());
  CHECK(std::equal(gap_then_prologue.begin(), gap_then_prologue.end(),
                   recovered.begin()));

  const std::array<uint8_t, 5> ones_inside_data = {
      {0xFF, 0xFF, 0x96, 0xD5, 0xAA}};
  REQUIRE(disk_encoding_nibbles_to_bits(
              ones_inside_data.data(), ones_inside_data.size(), nullptr,
              bits.data(), 128, &bit_count) == disk_err_none);
  CHECK(bit_count == 5 * 8);
  REQUIRE(disk_encoding_bits_to_nibbles(bits.data(), bit_count,
                                        recovered.data(), recovered.size(),
                                        &recovered_count) == disk_err_none);
  CHECK(recovered_count == ones_inside_data.size());
  CHECK(std::equal(ones_inside_data.begin(), ones_inside_data.end(),
                   recovered.begin()));
}

TEST_CASE("DiskGCR: [ADP-03] A track longer than the buffer is refused") {
  const std::array<uint8_t, 4> nibbles = {{0xFF, 0xFF, 0xFF, 0xD5}};
  const std::array<uint8_t, 4> all_sync = {{1, 1, 1, 1}};
  std::array<uint8_t, 8> bits{};
  uint32_t bit_count = 123;

  // Forty cells of medium will not fit in thirty-nine cells of buffer, and
  // the answer is a refusal rather than a track with its tail cut off.
  CHECK(disk_encoding_nibbles_to_bits(nibbles.data(), nibbles.size(),
                                      all_sync.data(), bits.data(), 39,
                                      &bit_count) == disk_err_unsupported);
  CHECK(bit_count == 0);
}

TEST_CASE("DiskGCR: [GCR-08] A flipped data nibble is refused, not decoded") {
  std::array<uint8_t, track_data_size> original_track{};
  populate_test_track(original_track);

  NibblizedTrack_t track = nibblize(
      original_track, disk_encoding_sector_order(disk_sector_order_dos), 0);

  // Sector 0's data field opens 71 nibbles in: gap 1 is 48, the address
  // field 14, its gap 6 and the data prologue 3.
  constexpr size_t first_data_field = 71;
  track.nibbles[first_data_field + 10] =
      static_cast<uint8_t>(track.nibbles[first_data_field + 10] ^ 0x01U);

  std::array<uint8_t, track_data_size> sectors{};
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  CHECK(disk_encoding_denibblize_track(
            disk_encoding_sector_order(disk_sector_order_dos), 0,
            track.nibbles.data(), track.count, sectors.data(),
            scratch.data()) == disk_err_corrupt);

  // One bad field and the whole track stays out of the image.
  for (size_t i = 0; i < track_data_size; ++i) {
    CHECK(sectors[i] == 0x00);
  }
}

TEST_CASE("DiskGCR: [GCR-09] An address field for another track is refused") {
  std::array<uint8_t, track_data_size> original_track{};
  populate_test_track(original_track);

  const NibblizedTrack_t track = nibblize(
      original_track, disk_encoding_sector_order(disk_sector_order_dos), 0);

  // The head reads a well-formed track that says it is track 0 while the
  // image is writing track 1: the sectors belong somewhere else.
  std::array<uint8_t, track_data_size> sectors{};
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  sectors.fill(0x7E);
  CHECK(disk_encoding_denibblize_track(
            disk_encoding_sector_order(disk_sector_order_dos), 1,
            track.nibbles.data(), track.count, sectors.data(),
            scratch.data()) == disk_err_corrupt);
  for (size_t i = 0; i < track_data_size; ++i) {
    CHECK(sectors[i] == 0x7E);
  }

  CHECK(disk_encoding_denibblize_track(
            disk_encoding_sector_order(disk_sector_order_dos), 0,
            track.nibbles.data(), track.count, sectors.data(),
            scratch.data()) == disk_err_none);
  for (size_t i = 0; i < track_data_size; ++i) {
    CHECK(sectors[i] == original_track[i]);
  }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
