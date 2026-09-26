// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Standard IEEE 802.3 CRC-32 (matches zlib and WOZ disk image specification).
namespace crc32_detail {
constexpr uint32_t reflected_polynomial = 0xEDB88320U;
constexpr uint32_t initial_state = 0xFFFFFFFFU;
constexpr size_t table_entries = 256;

using Table_t = std::array<uint32_t, table_entries>;

inline auto build_table() -> Table_t {
  Table_t table{};
  for (size_t i = 0; i < table.size(); ++i) {
    uint32_t crc = static_cast<uint32_t>(i);
    for (int bit = 0; bit < 8; ++bit) {
      crc = ((crc & 1U) != 0) ? (crc >> 1U) ^ reflected_polynomial : crc >> 1U;
    }
    table[i] = crc;
  }
  return table;
}

inline auto table() -> const Table_t& {
  static const Table_t shared_table = build_table();
  return shared_table;
}
}  // namespace crc32_detail

inline auto crc32_init() -> uint32_t { return crc32_detail::initial_state; }

inline auto crc32_update(uint32_t state, const void* data, size_t len)
    -> uint32_t {
  if (data == nullptr) {
    return state;
  }
  const auto* bytes = static_cast<const uint8_t*>(data);
  const auto& table = crc32_detail::table();
  for (size_t i = 0; i < len; ++i) {
    state = table[(state ^ bytes[i]) & 0xFFU] ^ (state >> 8);
  }
  return state;
}

inline auto crc32_final(uint32_t state) -> uint32_t {
  return state ^ crc32_detail::initial_state;
}

inline auto crc32_compute(const void* data, size_t len) -> uint32_t {
  return crc32_final(crc32_update(crc32_init(), data, len));
}
