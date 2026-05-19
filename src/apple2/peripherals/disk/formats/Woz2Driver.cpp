// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/Woz2Driver.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "core/Common.h"

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory,
//             bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification: This module uses procedural patterns for C-compatibility.
// google-runtime-int is required for fseek offsets. owning-memory and
// make-unique are suppressed for C++11 compatibility and handle-based
// resource management. easily-swappable-parameters is mandated by the
// Disk Driver ABI signatures.

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

struct WozInstance_t {
  FilePtr file{nullptr, fclose};
  std::array<uint8_t, woz::header_size> header{};
  uint32_t tmap_offset = 0;
  uint32_t trks_offset = 0;
  bool format_write_protected = false;
  bool os_readonly = false;

  WozInstance_t() = default;
  ~WozInstance_t() = default;

  WozInstance_t(const WozInstance_t&) = delete;
  auto operator=(const WozInstance_t&) -> WozInstance_t& = delete;
  WozInstance_t(WozInstance_t&&) = default;
  auto operator=(WozInstance_t&&) -> WozInstance_t& = default;
};

auto find_chunk(const uint8_t* header, const char* id) -> uint32_t {
  for (uint32_t i = woz::file_header_size;
       i < static_cast<uint32_t>(woz::header_size) - woz::chunk_header_size;) {
    if (memcmp(&header[i], id, woz::chunk_id_size) == 0) {
      return i + woz::chunk_header_size;
    }
    const uint32_t chunk_size =
        static_cast<uint32_t>(header[i + woz::chunk_size_offset_0]) |
        (static_cast<uint32_t>(header[i + woz::chunk_size_offset_1])
         << woz::bits_per_byte) |
        (static_cast<uint32_t>(header[i + woz::chunk_size_offset_2])
         << woz::shift_16) |
        (static_cast<uint32_t>(header[i + woz::chunk_size_offset_3])
         << woz::shift_24);
    i += woz::chunk_header_size + chunk_size;
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

static auto woz2_open(const char* path, uint32_t file_offset,
                      uint8_t enhanced_speed, bool* out_is_read_only,
                      void** out_instance) -> DiskError_e {
  if (path == nullptr || out_instance == nullptr) {
    return disk_err_io;
  }
  (void)enhanced_speed;
  auto wi_ptr = std::unique_ptr<WozInstance_t>(new WozInstance_t());

  wi_ptr->file.reset(fopen(path, "r+b"));
  if (wi_ptr->file != nullptr) {
    wi_ptr->os_readonly = false;
  } else {
    wi_ptr->file.reset(fopen(path, "rb"));
    if (wi_ptr->file != nullptr) {
      wi_ptr->os_readonly = true;
    } else {
      return disk_err_io;
    }
  }

  if (out_is_read_only != nullptr) {
    *out_is_read_only = wi_ptr->os_readonly;
  }

  if (fseek(wi_ptr->file.get(), static_cast<long>(file_offset), SEEK_SET) !=
      0) {
    return disk_err_io;
  }

  if (fread(wi_ptr->header.data(), 1, woz::header_size, wi_ptr->file.get()) !=
      static_cast<size_t>(woz::header_size)) {
    return disk_err_io;
  }

  const uint32_t info_ptr = find_chunk(wi_ptr->header.data(), "INFO");
  wi_ptr->tmap_offset = find_chunk(wi_ptr->header.data(), "TMAP");
  wi_ptr->trks_offset = find_chunk(wi_ptr->header.data(), "TRKS");

  if (info_ptr == 0 || wi_ptr->tmap_offset == 0 || wi_ptr->trks_offset == 0) {
    return disk_err_corrupt;
  }

  if (wi_ptr->header[info_ptr + woz::info_disk_type_offset] ==
      woz::disk_type_3_5) {
    return disk_err_unsupported_format;
  }

  wi_ptr->format_write_protected =
      (wi_ptr->header[info_ptr + woz::info_write_protect_offset] != 0);

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
  uint8_t nibble = 0;
  auto fetch_bit = [&](uint32_t idx) -> int {
    const uint32_t current_idx = idx % bit_count;
    return ((buffer[current_idx / woz::bits_per_byte] &
             (woz::bit_high_mask >> (current_idx % woz::bits_per_byte))) != 0)
               ? 1
               : 0;
  };

  while (fetch_bit(*bit_idx_ptr) == 0) {
    (*bit_idx_ptr)++;
  }

  for (int b = 0; b < woz::bits_per_byte; ++b) {
    nibble = static_cast<uint8_t>((nibble << 1) | fetch_bit((*bit_idx_ptr)++));
  }
  return nibble;
}

static void woz2_read_track(void* instance_handle, int track, int phase,
                          uint8_t* track_buffer, int* out_nibbles) {
  if (out_nibbles != nullptr) {
    *out_nibbles = 0;
  }

  if (instance_handle == nullptr || track_buffer == nullptr) {
    return;
  }
  (void)track;
  auto* wi_ptr = reinterpret_cast<WozInstance_t*>(instance_handle);


  const uint32_t tmap_index =
      static_cast<uint32_t>(phase) * (4 / phases_per_track);
  if (tmap_index >= static_cast<uint32_t>(woz::tmap_entries)) {
    if (out_nibbles != nullptr) {
      *out_nibbles = 0;
    }
    return;
  }

  const uint8_t trks_index = wi_ptr->header[wi_ptr->tmap_offset + tmap_index];
  if (trks_index == woz::unrecorded_track) {
    for (int i = 0; i < static_cast<int>(nibbles_per_track); ++i) {
      track_buffer[i] = static_cast<uint8_t>(rand() & woz::byte_mask);
    }
    if (out_nibbles != nullptr) {
      *out_nibbles = static_cast<int>(nibbles_per_track);
    }
    return;
  }

  if (trks_index >= woz::tmap_entries) {
    if (out_nibbles != nullptr) {
      *out_nibbles = 0;
    }
    return;
  }

  const uint8_t* trk =
      &wi_ptr->header[wi_ptr->trks_offset +
                  (static_cast<uint32_t>(trks_index) * woz::trks_entry_size)];
  const uint16_t starting_block =
      static_cast<uint16_t>(trk[0]) |
      (static_cast<uint16_t>(trk[1]) << woz::bits_per_byte);
  const uint16_t block_count =
      static_cast<uint16_t>(trk[2]) |
      (static_cast<uint16_t>(trk[3]) << woz::bits_per_byte);
  const uint32_t bit_count =
      static_cast<uint32_t>(trk[4]) |
      (static_cast<uint32_t>(trk[5]) << woz::bits_per_byte) |
      (static_cast<uint32_t>(trk[6]) << 16) |
      (static_cast<uint32_t>(trk[7]) << 24);

  if (bit_count == 0 || bit_count > static_cast<uint32_t>(block_count) *
                                        woz::data_block_size *
                                        woz::bits_per_byte) {
    if (out_nibbles != nullptr) {
      *out_nibbles = 0;
    }
    return;
  }

  const uint32_t byte_count =
      static_cast<uint32_t>(block_count) * woz::data_block_size;
  std::vector<uint8_t> buffer(byte_count);
  if (fseek(wi_ptr->file.get(),
            static_cast<long>(static_cast<uint32_t>(starting_block) *
                              woz::data_block_size),
            SEEK_SET) != 0) {
    if (out_nibbles != nullptr) {
      *out_nibbles = 0;
    }
    return;
  }
  if (fread(buffer.data(), 1, byte_count, wi_ptr->file.get()) != byte_count) {
    if (out_nibbles != nullptr) {
      *out_nibbles = 0;
    }
    return;
  }

  std::fill_n(track_buffer, nibbles_per_track, woz::byte_mask);
  uint32_t bit_idx = 0;
  int nibbles_done = 0;

  while (bit_idx < bit_count &&
         nibbles_done < static_cast<int>(nibbles_per_track)) {
    track_buffer[nibbles_done++] =
        reconstruct_bitstream_nibble(buffer.data(), bit_count, &bit_idx);
  }

  if (out_nibbles != nullptr) {
    *out_nibbles = nibbles_done;
  }
}

static auto woz2_command(void* instance, uint32_t cmd_id, const void* payload,
                         size_t payload_size) -> PeripheralStatus {
  (void)cmd_id;
  (void)payload;
  (void)payload_size;
  if (instance == nullptr) {
    return PERIPHERAL_ERROR;
  }
  return PERIPHERAL_INCOMPATIBLE;
}

extern "C" const DiskFormatDriver_t g_woz2_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = disk_driver_cap_write,
    .name = "WOZ 2",
    .creatable_exts = nullptr,
    .probe = woz2_probe,
    .open = woz2_open,
    .close = woz2_close,
    .is_write_protected = woz2_is_write_protected,
    .read_track = woz2_read_track,
    .write_track = nullptr,
    .create = nullptr,
    .command = woz2_command,
    .read_flux_bit = nullptr
};

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory,
//           bugprone-easily-swappable-parameters, modernize-make-unique)
