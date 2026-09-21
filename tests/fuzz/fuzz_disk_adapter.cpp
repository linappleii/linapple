// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"

extern "C" const char* __asan_default_options() { return "detect_leaks=1"; }

namespace {

constexpr uint8_t sync_byte = 0xFF;
constexpr uint32_t cells_per_nibble = 8;
constexpr uint32_t cells_per_self_sync = 10;

// A nibble the head could have read has its top bit set, so the fuzzer's
// bytes are lifted into that alphabet rather than thrown away.
auto as_nibble(uint8_t value) -> uint8_t {
  return static_cast<uint8_t>(value | 0x80U);
}

auto round_trips(const std::vector<uint8_t>& nibbles,
                 const std::vector<uint8_t>& bits, uint32_t bit_count) -> void {
  std::vector<uint8_t> recovered(nibbles_per_track, 0);
  uint32_t recovered_count = 0;
  assert(disk_encoding_bits_to_nibbles(bits.data(), bit_count, recovered.data(),
                                       nibbles_per_track,
                                       &recovered_count) == disk_err_none);
  assert(recovered_count == nibbles.size());
  for (size_t index = 0; index < nibbles.size(); ++index) {
    assert(recovered[index] == nibbles[index]);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2) {
    return 0;
  }

  const uint32_t count =
      static_cast<uint32_t>(size - 1) % (nibbles_per_track + 1);
  if (count == 0) {
    return 0;
  }

  // The first byte seeds which of the 0xFF nibbles the image calls self-sync,
  // because a nibble's own value cannot say whether the gap was recorded long.
  std::vector<uint8_t> nibbles(count, 0);
  std::vector<uint8_t> sync_mask(count, 0);
  uint32_t expected_cells = 0;
  for (uint32_t index = 0; index < count; ++index) {
    nibbles[index] = as_nibble(data[index + 1]);
    sync_mask[index] =
        (nibbles[index] == sync_byte && ((data[0] >> (index & 7U)) & 1U) != 0)
            ? 1
            : 0;
    expected_cells +=
        (sync_mask[index] != 0) ? cells_per_self_sync : cells_per_nibble;
  }

  std::vector<uint8_t> bits(max_track_bits / 8, 0);
  uint32_t bit_count = 0;
  const DiskError_e laid =
      disk_encoding_nibbles_to_bits(nibbles.data(), count, sync_mask.data(),
                                    bits.data(), max_track_bits, &bit_count);
  if (laid != disk_err_none) {
    assert(laid == disk_err_unsupported);
    assert(bit_count == 0);
    return 0;
  }

  // A data nibble is eight cells and a self-sync one ten, whatever the track
  // is made of.
  assert(bit_count == expected_cells);
  round_trips(nibbles, bits, bit_count);

  // The same track with no mask: the adapter has to decide for itself which
  // runs were gaps, and whatever it decides the nibbles still come back.
  uint32_t maskless_count = 0;
  assert(disk_encoding_nibbles_to_bits(nibbles.data(), count, nullptr,
                                       bits.data(), max_track_bits,
                                       &maskless_count) == disk_err_none);
  assert(maskless_count >= count * cells_per_nibble);
  assert(maskless_count <= count * cells_per_self_sync);
  round_trips(nibbles, bits, maskless_count);

  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
