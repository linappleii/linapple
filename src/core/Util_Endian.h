// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace endian_detail {
constexpr uint32_t byte_mask = 0xFFU;
constexpr unsigned shift_8 = 8U;
constexpr unsigned shift_16 = 16U;
constexpr unsigned shift_24 = 24U;
}  // namespace endian_detail

inline auto read_u16_le(const uint8_t* ptr) noexcept -> uint16_t {
  if (ptr == nullptr) {
    return 0;
  }
  return static_cast<uint16_t>(
      static_cast<uint32_t>(ptr[0]) |
      (static_cast<uint32_t>(ptr[1]) << endian_detail::shift_8));
}

inline auto read_u32_le(const uint8_t* ptr) noexcept -> uint32_t {
  if (ptr == nullptr) {
    return 0;
  }
  return static_cast<uint32_t>(ptr[0]) |
         (static_cast<uint32_t>(ptr[1]) << endian_detail::shift_8) |
         (static_cast<uint32_t>(ptr[2]) << endian_detail::shift_16) |
         (static_cast<uint32_t>(ptr[3]) << endian_detail::shift_24);
}

inline auto write_u16_le(uint8_t* ptr, uint16_t val) noexcept -> void {
  if (ptr == nullptr) {
    return;
  }
  ptr[0] = static_cast<uint8_t>(val & endian_detail::byte_mask);
  ptr[1] = static_cast<uint8_t>((val >> endian_detail::shift_8) &
                                endian_detail::byte_mask);
}

inline auto write_u32_le(uint8_t* ptr, uint32_t val) noexcept -> void {
  if (ptr == nullptr) {
    return;
  }
  ptr[0] = static_cast<uint8_t>(val & endian_detail::byte_mask);
  ptr[1] = static_cast<uint8_t>((val >> endian_detail::shift_8) &
                                endian_detail::byte_mask);
  ptr[2] = static_cast<uint8_t>((val >> endian_detail::shift_16) &
                                endian_detail::byte_mask);
  ptr[3] = static_cast<uint8_t>((val >> endian_detail::shift_24) &
                                endian_detail::byte_mask);
}

inline auto read_u16_unaligned(const uint8_t* ptr) noexcept -> uint16_t {
  return read_u16_le(ptr);
}
