// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The CRC-32 of zlib, PNG, gzip and the WOZ disk image specification: the
// reflected polynomial 0xEDB88320 with the state both seeded and finally
// inverted with 0xFFFFFFFF. A stream of crc32_update calls between crc32_init
// and crc32_final yields exactly what zlib's crc32(0, data, len) yields over
// the same bytes, so goldens computed with either agree.
namespace crc32_detail {
constexpr uint32_t reflected_polynomial = 0xEDB88320U;
constexpr uint32_t initial_state = 0xFFFFFFFFU;
constexpr size_t table_entries = 256;
constexpr int bits_per_byte = 8;
constexpr uint32_t byte_mask = 0xFFU;

using Table_t = std::array<uint32_t, table_entries>;

inline auto build_table() -> Table_t {
  Table_t table{};
  for (size_t i = 0; i < table.size(); ++i) {
    uint32_t crc = static_cast<uint32_t>(i);
    for (int bit = 0; bit < bits_per_byte; ++bit) {
      crc = ((crc & 1U) != 0) ? (crc >> 1U) ^ reflected_polynomial : crc >> 1U;
    }
    table[i] = crc;
  }
  return table;
}

// A function-local static gives every translation unit the one table and
// builds it on first use, which C++11 guarantees is thread-safe.
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
  const crc32_detail::Table_t& table = crc32_detail::table();
  for (size_t i = 0; i < len; ++i) {
    state = table[(state ^ bytes[i]) & crc32_detail::byte_mask] ^
            (state >> crc32_detail::bits_per_byte);
  }
  return state;
}

inline auto crc32_final(uint32_t state) -> uint32_t {
  return state ^ crc32_detail::initial_state;
}

inline auto crc32_compute(const void* data, size_t len) -> uint32_t {
  return crc32_final(crc32_update(crc32_init(), data, len));
}
