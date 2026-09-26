// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

inline auto read_u16_le(const uint8_t* ptr) -> uint16_t {
  if (ptr == nullptr) {
    return 0;
  }
  return static_cast<uint16_t>(static_cast<uint32_t>(ptr[0]) |
                               (static_cast<uint32_t>(ptr[1]) << 8));
}

inline auto read_u32_le(const uint8_t* ptr) -> uint32_t {
  if (ptr == nullptr) {
    return 0;
  }
  return static_cast<uint32_t>(ptr[0]) | (static_cast<uint32_t>(ptr[1]) << 8) |
         (static_cast<uint32_t>(ptr[2]) << 16) |
         (static_cast<uint32_t>(ptr[3]) << 24);
}

inline auto write_u16_le(uint8_t* ptr, uint16_t val) -> void {
  if (ptr == nullptr) {
    return;
  }
  ptr[0] = static_cast<uint8_t>(val & 0xFF);
  ptr[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

inline auto write_u32_le(uint8_t* ptr, uint32_t val) -> void {
  if (ptr == nullptr) {
    return;
  }
  ptr[0] = static_cast<uint8_t>(val & 0xFF);
  ptr[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
  ptr[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
  ptr[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

inline auto read_u16_unaligned(const uint8_t* ptr) -> uint16_t {
  return read_u16_le(ptr);
}
