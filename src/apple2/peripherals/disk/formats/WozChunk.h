// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/Util_Endian.h"

// The chunk container that WOZ 1.0 and 2.0 share: a twelve-byte file header
// followed by (id, little-endian size, data) records. The two versions differ
// only in how they lay out TRKS, so each driver keeps its own track reader.
namespace woz {
constexpr size_t signature_len = 8;
constexpr int file_header_size = 12;
constexpr int chunk_id_size = 4;
constexpr int chunk_header_size = 8;
constexpr int chunk_size_offset = 4;
constexpr int tmap_entries = 160;
constexpr uint8_t unrecorded_track = 0xFF;

constexpr int info_disk_type_offset = 1;
constexpr int info_write_protect_offset = 2;
constexpr int disk_type_3_5 = 2;

constexpr int bits_per_byte = 8;
}  // namespace woz

inline auto woz_header_at(const uint8_t* header, size_t header_len,
                          uint64_t offset, size_t len) -> const uint8_t* {
  if (header == nullptr || offset > header_len || len > header_len ||
      (offset + len) > header_len || (offset + len) < offset) {
    return nullptr;
  }
  return header + offset;
}

// Returns the offset of the chunk's data, or 0 when no such chunk lies within
// the header; the chunk's declared size lands in out_size when it is wanted.
inline auto woz_find_chunk(const uint8_t* header, size_t header_len,
                           const char* id, uint32_t* out_size) -> uint32_t {
  for (uint32_t i = woz::file_header_size;;) {
    const uint8_t* const chunk_hdr =
        woz_header_at(header, header_len, i, woz::chunk_header_size);
    if (chunk_hdr == nullptr) {
      break;
    }
    const uint32_t chunk_size = read_u32_le(&chunk_hdr[woz::chunk_size_offset]);
    if (memcmp(chunk_hdr, id, woz::chunk_id_size) == 0) {
      if (out_size != nullptr) {
        *out_size = chunk_size;
      }
      return i + woz::chunk_header_size;
    }
    const uint64_t next_i =
        static_cast<uint64_t>(i) + woz::chunk_header_size + chunk_size;
    if (next_i <= i ||
        woz_header_at(header, header_len, next_i, 0) == nullptr) {
      break;
    }
    i = static_cast<uint32_t>(next_i);
  }
  return 0;
}
