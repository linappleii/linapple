// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) Justification:
// Byte-level binary decoding requires indexing into unaligned byte arrays.

inline auto read_u16_le(const uint8_t* ptr) -> uint16_t {
  if (ptr == nullptr) {
    return 0;
  }
  constexpr int bits_per_byte = 8;
  return static_cast<uint16_t>(
      static_cast<uint16_t>(ptr[0]) |
      static_cast<uint16_t>(static_cast<uint16_t>(ptr[1]) << bits_per_byte));
}

inline auto read_u32_le(const uint8_t* ptr) -> uint32_t {
  if (ptr == nullptr) {
    return 0;
  }
  constexpr int shift_8 = 8;
  constexpr int shift_16 = 16;
  constexpr int shift_24 = 24;
  return static_cast<uint32_t>(ptr[0]) |
         (static_cast<uint32_t>(ptr[1]) << shift_8) |
         (static_cast<uint32_t>(ptr[2]) << shift_16) |
         (static_cast<uint32_t>(ptr[3]) << shift_24);
}

inline auto read_u16_unaligned(const uint8_t* ptr) -> uint16_t {
  return read_u16_le(ptr);
}

inline auto read_u32_unaligned(const uint8_t* ptr) -> uint32_t {
  return read_u32_le(ptr);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
