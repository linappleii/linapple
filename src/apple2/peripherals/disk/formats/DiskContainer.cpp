// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/DiskContainer.h"

#include <cstring>

// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-bounds-pointer-arithmetic) Justification:
// Domain-specific container detection requires parameters mandated by the
// shared format probing signatures. Pointer arithmetic is required for physical
// bitstream header inspection.

namespace macbinary {
namespace {
constexpr uint8_t version_offset = 0;
constexpr uint8_t secondary_offset = 122;
constexpr uint8_t name_len_offset = 1;
constexpr uint8_t min_version = 0;
constexpr uint8_t secondary_zero = 0;
constexpr uint8_t max_name_len = 63;
}  // namespace
}  // namespace macbinary

// Why: Implements physical bitstream inspection to detect MacBinary II/III
// wrappers, a legacy container format used to store Apple II disk images
// with Macintosh-specific resource forks.
extern "C" auto disk_container_detect_macbinary(const uint8_t* header_data,
                                                size_t header_size,
                                                uint32_t file_size)
    -> uint32_t {
  if (header_data == nullptr || header_size < macbinary::header_size ||
      file_size <= macbinary::header_size) {
    return 0;
  }

  if (header_data[macbinary::version_offset] == macbinary::min_version &&
      header_data[macbinary::secondary_offset] == macbinary::secondary_zero) {
    const uint8_t name_len = header_data[macbinary::name_len_offset];
    if (name_len > 0 && name_len <= macbinary::max_name_len) {
      return static_cast<uint32_t>(macbinary::header_size);
    }
  }

  return 0;
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
