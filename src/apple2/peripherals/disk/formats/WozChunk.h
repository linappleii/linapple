// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "apple2/peripherals/disk/DiskError.h"
#include "core/Util_Crc32.h"
#include "core/Util_Endian.h"
#include "core/Util_Path.h"

// The chunk container that WOZ 1.0 and 2.0 share: a twelve-byte file header
// followed by (id, little-endian size, data) records. The versions differ in
// their INFO fields and in how they lay out TRKS, so each driver reads its
// own INFO and keeps its own track reader.
namespace woz {
constexpr size_t signature_len = 8;
constexpr int file_header_size = 12;
constexpr int crc32_offset = 8;
constexpr size_t crc32_stream_buffer_size = 4096;
constexpr int chunk_id_size = 4;
constexpr int chunk_header_size = 8;
constexpr int chunk_size_offset = 4;
constexpr int tmap_entries = 160;
constexpr uint8_t unrecorded_track = 0xFF;

// INFO version 1 is a WOZ1 file, 2 a WOZ2 file and 3 a WOZ 2.1 file; the
// specification names 5.25" as disk type 1 and 3.5" as 2, nothing else.
constexpr int info_version_offset = 0;
constexpr int info_disk_type_offset = 1;
constexpr int info_write_protect_offset = 2;
constexpr int disk_type_5_25 = 1;

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

// The specification's chunk IDs are four ASCII characters, so a header whose
// bytes are not all printable is not a chunk. That is how the walk tells a
// MacBinary data fork's zero padding, or a resource fork, from the image.
inline auto woz_is_chunk_id(const uint8_t* id) -> bool {
  for (int i = 0; i < woz::chunk_id_size; ++i) {
    if (id[i] < 0x20 || id[i] > 0x7E) {
      return false;
    }
  }
  return true;
}

// Walks the chunk list on disk from the file header and returns the offset,
// relative to base_offset, one past the last complete chunk. A chunk whose
// declared size runs past the end of the file is not counted.
inline auto woz_chunks_end(FILE* file, uint32_t base_offset, uint64_t file_size)
    -> uint64_t {
  uint64_t end = woz::file_header_size;
  for (;;) {
    std::array<uint8_t, woz::chunk_header_size> chunk_hdr{};
    const uint64_t hdr_pos = static_cast<uint64_t>(base_offset) + end;
    if (hdr_pos + chunk_hdr.size() > file_size ||
        fseek(file, static_cast<long>(hdr_pos), SEEK_SET) != 0 ||
        fread(chunk_hdr.data(), 1, chunk_hdr.size(), file) !=
            chunk_hdr.size() ||
        !woz_is_chunk_id(chunk_hdr.data())) {
      return end;
    }
    const uint64_t next = end + chunk_hdr.size() +
                          read_u32_le(&chunk_hdr[woz::chunk_size_offset]);
    if (static_cast<uint64_t>(base_offset) + next > file_size) {
      return end;
    }
    end = next;
  }
}

// The specification defines the CRC32 over everything after the twelve-byte
// file header, and a zero CRC as "not computed". The image may sit inside a
// container whose padding or resource fork follows the last chunk, so the
// range ends at the last chunk rather than at the end of the file.
inline auto woz_verify_crc32(FILE* file, uint32_t base_offset,
                             uint32_t stored_crc) -> DiskError_e {
  if (file == nullptr) {
    return disk_err_invalid_argument;
  }
  if (stored_crc == 0) {
    return disk_err_none;
  }
  const int64_t file_size = Path::file_size(file);
  if (file_size < 0) {
    return disk_err_io;
  }
  const uint64_t end =
      woz_chunks_end(file, base_offset, static_cast<uint64_t>(file_size));
  if (fseek(file,
            static_cast<long>(static_cast<uint64_t>(base_offset) +
                              woz::file_header_size),
            SEEK_SET) != 0) {
    return disk_err_io;
  }
  std::array<uint8_t, woz::crc32_stream_buffer_size> buffer{};
  uint32_t state = crc32_init();
  for (uint64_t remaining = end - woz::file_header_size; remaining > 0;) {
    const size_t want = remaining < buffer.size()
                            ? static_cast<size_t>(remaining)
                            : buffer.size();
    if (fread(buffer.data(), 1, want, file) != want) {
      return disk_err_io;
    }
    state = crc32_update(state, buffer.data(), want);
    remaining -= want;
  }
  return crc32_final(state) == stored_crc ? disk_err_none : disk_err_corrupt;
}
