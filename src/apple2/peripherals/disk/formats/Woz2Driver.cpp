// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/Woz2Driver.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/DiskFormatRegistration.h"
#include "core/Util_Endian.h"
#include "core/Util_Path.h"

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification:
// This module uses procedural patterns for C-compatibility. google-runtime-int
// is required for fseek offsets. owning-memory and make-unique are suppressed
// for C++11 compatibility and handle-based resource management.
// easily-swappable-parameters is mandated by the Disk Driver ABI signatures.

namespace {
namespace woz {
constexpr char signature[] = "WOZ2\xFF\n\r\n";
constexpr size_t signature_len = 8;
constexpr int header_size = 1536;
constexpr int data_block_size = 512;
constexpr uint8_t unrecorded_track = 0xFF;

constexpr int chunk_id_size = 4;
constexpr int chunk_header_size = 8;
constexpr int file_header_size = 12;
constexpr int tmap_entries = 160;
constexpr int trks_entry_size = 8;
constexpr uint16_t max_track_blocks = 64;

constexpr int info_disk_type_offset = 1;
constexpr int info_write_protect_offset = 2;
constexpr int disk_type_3_5 = 2;

constexpr int bits_per_byte = 8;
constexpr int shift_16 = 16;
constexpr int shift_24 = 24;
constexpr uint8_t bit_high_mask = 0x80;
constexpr uint8_t byte_mask = 0xFF;

constexpr int chunk_size_offset_0 = 4;
constexpr int chunk_size_offset_1 = 5;
constexpr int chunk_size_offset_2 = 6;
constexpr int chunk_size_offset_3 = 7;
}  // namespace woz

inline auto header_at(const uint8_t* header, size_t header_len, uint64_t offset,
                      size_t len) -> const uint8_t* {
  if (header == nullptr || offset > header_len || len > header_len ||
      (offset + len) > header_len || (offset + len) < offset) {
    return nullptr;
  }
  return header + offset;
}

struct WozInstance_t {
  FilePtr_t file{nullptr, fclose};
  std::array<uint8_t, woz::header_size> header{};
  uint32_t tmap_offset = 0;
  uint32_t trks_offset = 0;
  std::array<uint8_t, nibbles_per_track> nibbles{};
  bool format_write_protected = false;
  bool os_readonly = false;

  WozInstance_t() = default;
  ~WozInstance_t() = default;

  WozInstance_t(const WozInstance_t&) = delete;
  auto operator=(const WozInstance_t&) -> WozInstance_t& = delete;
  WozInstance_t(WozInstance_t&&) = default;
  auto operator=(WozInstance_t&&) -> WozInstance_t& = default;

  auto header_at(uint64_t offset, size_t len) const -> const uint8_t* {
    return ::header_at(header.data(), header.size(), offset, len);
  }
};

auto find_chunk(const uint8_t* header, size_t header_len, const char* id)
    -> uint32_t {
  for (uint32_t i = woz::file_header_size;;) {
    const uint8_t* const chunk_hdr =
        header_at(header, header_len, i, woz::chunk_header_size);
    if (chunk_hdr == nullptr) {
      break;
    }
    if (memcmp(chunk_hdr, id, woz::chunk_id_size) == 0) {
      return i + woz::chunk_header_size;
    }
    const uint32_t chunk_size =
        read_u32_le(&chunk_hdr[woz::chunk_size_offset_0]);
    const uint64_t next_i =
        static_cast<uint64_t>(i) + woz::chunk_header_size + chunk_size;
    if (next_i <= i || header_at(header, header_len, next_i, 0) == nullptr) {
      break;
    }
    i = static_cast<uint32_t>(next_i);
  }
  return 0;
}
}  // namespace

static auto woz2_probe(const uint8_t* header_data, size_t header_size,
                       uint32_t file_size, const char* ext_hint)
    -> DiskProbe_e {
  (void)ext_hint;

  if (header_size < woz::signature_len || file_size < woz::header_size) {
    return disk_probe_no;
  }

  if (memcmp(header_data, woz::signature, woz::signature_len) == 0) {
    return disk_probe_definite;
  }

  return disk_probe_no;
}

static auto woz2_open(const char* path, uint32_t file_offset, bool read_only,
                      void** out_instance) -> DiskError_e {
  if (path == nullptr || out_instance == nullptr) {
    return disk_err_io;
  }
  auto wi_ptr = std::unique_ptr<WozInstance_t>(new WozInstance_t());

  wi_ptr->os_readonly = read_only;
  if (!read_only) {
    wi_ptr->file.reset(fopen(path, "r+b"));
  }
  if (wi_ptr->file == nullptr) {
    wi_ptr->file.reset(fopen(path, "rb"));
    wi_ptr->os_readonly = true;
  }
  if (wi_ptr->file == nullptr) {
    return disk_err_io;
  }

  if (fseek(wi_ptr->file.get(), static_cast<long>(file_offset), SEEK_SET) !=
      0) {
    return disk_err_io;
  }

  if (fread(wi_ptr->header.data(), 1, woz::header_size, wi_ptr->file.get()) !=
      static_cast<size_t>(woz::header_size)) {
    return disk_err_io;
  }

  const uint32_t info_ptr =
      find_chunk(wi_ptr->header.data(), wi_ptr->header.size(), "INFO");
  wi_ptr->tmap_offset =
      find_chunk(wi_ptr->header.data(), wi_ptr->header.size(), "TMAP");
  wi_ptr->trks_offset =
      find_chunk(wi_ptr->header.data(), wi_ptr->header.size(), "TRKS");

  if (info_ptr == 0 || wi_ptr->tmap_offset == 0 || wi_ptr->trks_offset == 0) {
    return disk_err_corrupt;
  }

  const uint8_t* const info_data =
      wi_ptr->header_at(info_ptr, woz::info_write_protect_offset + 1);
  if (info_data == nullptr ||
      wi_ptr->header_at(wi_ptr->tmap_offset, woz::tmap_entries) == nullptr ||
      wi_ptr->header_at(wi_ptr->trks_offset, woz::trks_entry_size) == nullptr) {
    return disk_err_corrupt;
  }

  if (info_data[woz::info_disk_type_offset] == woz::disk_type_3_5) {
    return disk_err_unsupported_format;
  }

  wi_ptr->format_write_protected =
      (info_data[woz::info_write_protect_offset] != 0);

  *out_instance = reinterpret_cast<void*>(wi_ptr.release());
  return disk_err_none;
}

static void woz2_close(void* instance) {
  if (instance == nullptr) {
    return;
  }
  delete reinterpret_cast<WozInstance_t*>(instance);
}

static auto woz2_is_write_protected(void* instance) -> bool {
  if (instance == nullptr) {
    return true;
  }
  auto* wi_ptr = reinterpret_cast<WozInstance_t*>(instance);
  return wi_ptr->os_readonly || wi_ptr->format_write_protected;
}

// Why: Reconstructs an Apple II nibble from the raw flux bitstream.
// Searches for the next sync-bit (1) and then gathers 8 bits to form a byte.
auto reconstruct_bitstream_nibble(const uint8_t* buffer, uint32_t bit_count,
                                  uint32_t* bit_idx_ptr) -> uint8_t {
  if (bit_count == 0 || buffer == nullptr || bit_idx_ptr == nullptr) {
    return 0;
  }
  uint8_t nibble = 0;
  auto fetch_bit = [&](uint32_t idx) -> int {
    const uint32_t current_idx = idx % bit_count;
    return ((buffer[current_idx / woz::bits_per_byte] &
             (woz::bit_high_mask >> (current_idx % woz::bits_per_byte))) != 0)
               ? 1
               : 0;
  };

  uint32_t search_limit = bit_count;
  while (fetch_bit(*bit_idx_ptr) == 0 && search_limit > 0) {
    (*bit_idx_ptr)++;
    search_limit--;
  }

  for (int b = 0; b < woz::bits_per_byte; ++b) {
    nibble = static_cast<uint8_t>((nibble << 1) | fetch_bit((*bit_idx_ptr)++));
  }
  return nibble;
}

static auto woz2_read_track_bits(void* instance_handle, uint32_t quarter_track,
                                 uint8_t* bits, uint32_t max_bits,
                                 uint32_t* out_bit_count) -> DiskError_e {
  if (instance_handle == nullptr || bits == nullptr ||
      out_bit_count == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_bit_count = 0;

  auto* wi_ptr = reinterpret_cast<WozInstance_t*>(instance_handle);

  if (quarter_track >= static_cast<uint32_t>(woz::tmap_entries)) {
    return disk_err_invalid_argument;
  }

  const uint8_t* const tmap_entry =
      wi_ptr->header_at(wi_ptr->tmap_offset + quarter_track, 1);
  if (tmap_entry == nullptr) {
    return disk_err_corrupt;
  }

  // An unmapped quarter track is surface the image never recorded, which the
  // card hears as noise rather than as a failure to read.
  const uint8_t trks_index = *tmap_entry;
  if (trks_index == woz::unrecorded_track) {
    return disk_err_none;
  }

  if (trks_index >= woz::tmap_entries) {
    return disk_err_corrupt;
  }

  const uint64_t entry_offset =
      static_cast<uint64_t>(wi_ptr->trks_offset) +
      (static_cast<uint64_t>(trks_index) * woz::trks_entry_size);
  const uint8_t* const trk =
      wi_ptr->header_at(entry_offset, woz::trks_entry_size);
  if (trk == nullptr) {
    return disk_err_corrupt;
  }
  const uint16_t starting_block = read_u16_le(&trk[0]);
  const uint16_t block_count = read_u16_le(&trk[2]);
  const uint32_t bit_count = read_u32_le(&trk[4]);

  if (block_count == 0 || block_count > woz::max_track_blocks) {
    return disk_err_corrupt;
  }

  const uint32_t byte_count =
      static_cast<uint32_t>(block_count) * woz::data_block_size;

  if (bit_count == 0 || bit_count > byte_count * woz::bits_per_byte) {
    return disk_err_corrupt;
  }

  const int64_t total_file_size = Path::file_size(wi_ptr->file.get());
  const uint64_t file_offset =
      static_cast<uint64_t>(starting_block) * woz::data_block_size;
  if (file_offset + byte_count > static_cast<uint64_t>(total_file_size)) {
    return disk_err_corrupt;
  }

  std::vector<uint8_t> buffer(byte_count);
  if (fseek(wi_ptr->file.get(), static_cast<long>(file_offset), SEEK_SET) !=
      0) {
    return disk_err_io;
  }
  if (fread(buffer.data(), 1, byte_count, wi_ptr->file.get()) != byte_count) {
    return disk_err_io;
  }

  wi_ptr->nibbles.fill(woz::byte_mask);
  uint32_t bit_idx = 0;
  uint32_t nibbles_done = 0;

  while (bit_idx < bit_count && nibbles_done < nibbles_per_track) {
    wi_ptr->nibbles[nibbles_done++] =
        reconstruct_bitstream_nibble(buffer.data(), bit_count, &bit_idx);
  }

  return disk_encoding_nibbles_to_bits(wi_ptr->nibbles.data(), nibbles_done,
                                       bits, max_bits, out_bit_count);
}

const char* const g_woz2_supported_exts[] = {"woz", nullptr};

extern "C" const DiskFormatDriver_t g_woz2_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = 0,
    .name = "WOZ 2",
    .supported_exts = g_woz2_supported_exts,
    .probe = woz2_probe,
    .open = woz2_open,
    .close = woz2_close,
    .is_write_protected = woz2_is_write_protected,
    .read_track_bits = woz2_read_track_bits,
    .write_track_bits = nullptr,
    .create = nullptr};

static const DiskFormatRegistration_t k_reg{&g_woz2_driver};

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
