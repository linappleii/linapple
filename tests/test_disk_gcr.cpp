// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "doctest.h"

namespace {

constexpr size_t sector_size = 256;
constexpr size_t track_data_size = sectors_per_track * sector_size;  // 4096
constexpr uint32_t expected_nibble_count = 6160;

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

auto execute_round_trip(int track_num, bool is_dos_order) -> void {
  std::array<uint8_t, track_data_size> original_track{};
  populate_test_track(original_track);

  std::array<uint8_t, disk_encoding_work_buffer_size> work_buffer{};
  std::copy(original_track.begin(), original_track.end(), work_buffer.begin());

  std::array<uint8_t, nibbles_per_track> track_image{};

  const uint32_t nibbles = disk_encoding_nibblize_track(
      work_buffer.data(), track_image.data(), is_dos_order, track_num);

  CHECK(nibbles == expected_nibble_count);

  std::fill(work_buffer.begin(), work_buffer.end(), static_cast<uint8_t>(0));

  disk_encoding_denibblize_track(work_buffer.data(), track_image.data(),
                                 is_dos_order,
                                 static_cast<int>(nibbles_per_track));

  for (size_t i = 0; i < track_data_size; ++i) {
    CHECK(work_buffer[i] == original_track[i]);
  }
}

}  // namespace

TEST_CASE(
    "DiskGCR: [GCR-01] disk_encoding_work_buffer_size ABI Constant "
    "Validation") {
  CHECK(disk_encoding_work_buffer_size == 0x3000);
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

    std::array<uint8_t, disk_encoding_work_buffer_size> work_buffer{};
    std::copy(original_track.begin(), original_track.end(),
              work_buffer.begin());

    std::array<uint8_t, nibbles_per_track> track_image{};

    const uint32_t nibbles = disk_encoding_nibblize_track_custom_order(
        work_buffer.data(), track_image.data(), custom_dos_order.data(), 1);

    CHECK(nibbles == expected_nibble_count);

    std::fill(work_buffer.begin(), work_buffer.end(), static_cast<uint8_t>(0));

    disk_encoding_denibblize_track(work_buffer.data(), track_image.data(),
                                   true /* is_dos_order */,
                                   static_cast<int>(nibbles_per_track));

    for (size_t i = 0; i < track_data_size; ++i) {
      CHECK(work_buffer[i] == original_track[i]);
    }
  }

  SUBCASE("Explicit ProDOS sector order round-trip") {
    const std::array<uint8_t, sectors_per_track> custom_prodos_order = {
        {0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D,
         0x06, 0x0E, 0x07, 0x0F}};

    std::array<uint8_t, track_data_size> original_track{};
    populate_test_track(original_track);

    std::array<uint8_t, disk_encoding_work_buffer_size> work_buffer{};
    std::copy(original_track.begin(), original_track.end(),
              work_buffer.begin());

    std::array<uint8_t, nibbles_per_track> track_image{};

    const uint32_t nibbles = disk_encoding_nibblize_track_custom_order(
        work_buffer.data(), track_image.data(), custom_prodos_order.data(), 2);

    CHECK(nibbles == expected_nibble_count);

    std::fill(work_buffer.begin(), work_buffer.end(), static_cast<uint8_t>(0));

    disk_encoding_denibblize_track(work_buffer.data(), track_image.data(),
                                   false /* is_dos_order */,
                                   static_cast<int>(nibbles_per_track));

    for (size_t i = 0; i < track_data_size; ++i) {
      CHECK(work_buffer[i] == original_track[i]);
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

    std::array<uint8_t, disk_encoding_work_buffer_size> work_buffer{};
    std::copy(original_track.begin(), original_track.end(),
              work_buffer.begin());

    std::array<uint8_t, nibbles_per_track> track_image{};

    const uint32_t nibbles = disk_encoding_nibblize_track_custom_order(
        work_buffer.data(), track_image.data(), custom_rev_order.data(), 5);

    CHECK(nibbles == expected_nibble_count);

    std::fill(work_buffer.begin(), work_buffer.end(), static_cast<uint8_t>(0));

    disk_encoding_denibblize_track(work_buffer.data(), track_image.data(),
                                   true /* is_dos_order */,
                                   static_cast<int>(nibbles_per_track));

    for (size_t sec = 0; sec < sectors_per_track; ++sec) {
      const size_t original_base = custom_rev_order[sec] * sector_size;
      const size_t decoded_base = standard_dos_order[sec] * sector_size;
      for (size_t b = 0; b < sector_size; ++b) {
        CHECK(work_buffer[decoded_base + b] ==
              original_track[original_base + b]);
      }
    }
  }
}

TEST_CASE(
    "DiskGCR: [GCR-05] Physical GCR 6-and-2 Bit Invariants Across Encoded "
    "Track") {
  std::array<uint8_t, track_data_size> original_track{};
  populate_test_track(original_track);

  std::array<uint8_t, disk_encoding_work_buffer_size> work_buffer{};
  std::copy(original_track.begin(), original_track.end(), work_buffer.begin());

  std::array<uint8_t, nibbles_per_track> track_image{};

  const uint32_t nibbles = disk_encoding_nibblize_track(
      work_buffer.data(), track_image.data(), true, 0);

  CHECK(nibbles == expected_nibble_count);

  // Invariant 1: Physical disk nibbles must always have bit 7 (MSB) set
  for (size_t i = 0; i < nibbles_per_track; ++i) {
    CHECK((track_image[i] & gcr_high_bit_mask) != 0U);
  }

  // Invariant 2: Trailing track padding beyond sector data must be 0xFF sync
  // bytes
  for (size_t i = expected_nibble_count; i < nibbles_per_track; ++i) {
    CHECK(track_image[i] == sync_byte);
  }
}

TEST_CASE("DiskGCR: [GCR-06] Skewed Track Rotational Phase Invariance") {
  const std::array<int, 4> test_tracks = {{0, 1, 17, 34}};

  for (size_t t = 0; t < test_tracks.size(); ++t) {
    const int track_num = test_tracks[t];

    std::array<uint8_t, track_data_size> original_track{};
    populate_test_track(original_track);

    std::array<uint8_t, disk_encoding_work_buffer_size> work_buffer{};
    std::copy(original_track.begin(), original_track.end(),
              work_buffer.begin());

    std::array<uint8_t, nibbles_per_track> track_image{};

    const uint32_t nibbles = disk_encoding_nibblize_track(
        work_buffer.data(), track_image.data(), true, track_num);

    CHECK(nibbles == expected_nibble_count);

    // Skew the track buffer across the rotational surface
    disk_encoding_skew_track(track_image.data(), work_buffer.data(), track_num,
                             static_cast<int>(nibbles));

    std::fill(work_buffer.begin(), work_buffer.end(), static_cast<uint8_t>(0));

    disk_encoding_denibblize_track(work_buffer.data(), track_image.data(), true,
                                   static_cast<int>(nibbles));

    for (size_t i = 0; i < track_data_size; ++i) {
      CHECK(work_buffer[i] == original_track[i]);
    }
  }
}

TEST_CASE(
    "DiskGCR: [GCR-07] Corrupted and Malformed Nibble Streams Robustness") {
  std::array<uint8_t, disk_encoding_work_buffer_size> work_buffer{};
  std::array<uint8_t, nibbles_per_track> track_image{};

  SUBCASE("Unformatted track (all 0xFF sync bytes) produces zeroed output") {
    track_image.fill(sync_byte);
    work_buffer.fill(0xAA);

    disk_encoding_denibblize_track(work_buffer.data(), track_image.data(), true,
                                   static_cast<int>(nibbles_per_track));

    for (size_t i = 0; i < track_data_size; ++i) {
      CHECK(work_buffer[i] == 0x00);
    }
  }

  SUBCASE("Zero-filled track (all 0x00) safely zeroed without crash") {
    track_image.fill(0x00);
    work_buffer.fill(0x55);

    disk_encoding_denibblize_track(work_buffer.data(), track_image.data(), true,
                                   static_cast<int>(nibbles_per_track));

    for (size_t i = 0; i < track_data_size; ++i) {
      CHECK(work_buffer[i] == 0x00);
    }
  }

  SUBCASE("Truncated nibble stream counts do not overrun buffers") {
    const std::array<int, 4> truncated_lengths = {{100, 256, 512, 1024}};

    for (size_t i = 0; i < truncated_lengths.size(); ++i) {
      work_buffer.fill(0x33);
      track_image.fill(sync_byte);

      disk_encoding_denibblize_track(work_buffer.data(), track_image.data(),
                                     true, truncated_lengths[i]);

      for (size_t b = 0; b < track_data_size; ++b) {
        CHECK(work_buffer[b] == 0x00);
      }
    }
  }

  SUBCASE("Out-of-range sector number in address field safely ignored") {
    std::array<uint8_t, track_data_size> original_track{};
    populate_test_track(original_track);

    std::copy(original_track.begin(), original_track.end(),
              work_buffer.begin());
    disk_encoding_nibblize_track(work_buffer.data(), track_image.data(), true,
                                 0);

    // Sector 0 address field is at byte 0:
    // [0..2]: D5 AA 96
    // [3..4]: Volume (FE)
    // [5..6]: Track (00)
    // [7..8]: Sector 4-and-4
    // Inject invalid sector number 25 (valid sectors are 0..15)
    track_image[7] = encode_4and4_high(25);
    track_image[8] = encode_4and4_low(25);

    work_buffer.fill(0x00);
    disk_encoding_denibblize_track(work_buffer.data(), track_image.data(), true,
                                   static_cast<int>(nibbles_per_track));

    // Sector 0 was bypassed due to sector range violation; work_buffer must not
    // be corrupted
    for (size_t b = 0; b < sector_size; ++b) {
      CHECK(work_buffer[b] == 0x00);
    }
  }
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
