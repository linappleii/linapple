// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/IieDriver.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "core/Util_Path.h"

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory,
// bugprone-easily-swappable-parameters, modernize-make-unique) Justification:
// This module uses procedural patterns for C-compatibility. google-runtime-int
// is required for fseek offsets. owning-memory and make-unique are suppressed
// for C++11 compatibility and handle-based resource management.
// easily-swappable-parameters is mandated by the Disk Driver ABI signatures.

namespace {
namespace iie {
static constexpr std::array<uint8_t, 13> signature = {
    'S', 'I', 'M', 'S', 'Y', 'S', 'T', 'E', 'M', '_', 'I', 'I', 'E'};
constexpr size_t signature_len = 13;
constexpr int header_size = 88;
constexpr int track_data_offset = 30;
constexpr int tracks = 35;

constexpr int variant_offset = 13;
constexpr int sector_map_offset = 14;
constexpr int nibble_map_offset = 14;

constexpr uint8_t variant_max_legacy = 2;
constexpr uint8_t variant_max_total = 3;
constexpr uint8_t sector_not_found = 0xFF;
}  // namespace iie

namespace dos {
constexpr int track_size = 4096;
}

struct IieInstance_t {
  FilePtr_t file{nullptr, fclose};
  std::array<uint8_t, iie::header_size> header{};
  std::array<uint8_t, sectors_per_track> sector_order{};
  std::array<uint8_t, disk_encoding_work_buffer_offset * 3> work_buffer{};
  std::array<uint32_t, iie::tracks> track_offsets{};
  std::array<uint16_t, iie::tracks> track_nibble_counts{};
  bool os_readonly = false;

  IieInstance_t() = default;
  ~IieInstance_t() = default;

  IieInstance_t(const IieInstance_t&) = delete;
  auto operator=(const IieInstance_t&) -> IieInstance_t& = delete;
  IieInstance_t(IieInstance_t&&) = default;
  auto operator=(IieInstance_t&&) -> IieInstance_t& = default;
};

inline auto read_u16_le(const uint8_t* data_ptr) -> uint16_t {
  if (data_ptr == nullptr) {
    return 0;
  }

  constexpr int bits_per_byte = 8;
  const uint16_t low_byte = static_cast<uint16_t>(data_ptr[0]);
  const uint16_t high_byte = static_cast<uint16_t>(data_ptr[1]);

  return static_cast<uint16_t>(low_byte | (high_byte << bits_per_byte));
}

// Why: Reverses a sector mapping. SimSystem //e images store custom sector
// ordering that must be inverted into a standard lookup table for the
// nibblizer.
auto iie_convert_sector_order(const uint8_t* source_order,
                              uint8_t* sector_order) -> void {
  if (source_order == nullptr || sector_order == nullptr) {
    return;
  }

  for (int target_sector = 0; target_sector < sectors_per_track;
       ++target_sector) {
    uint8_t found_index = iie::sector_not_found;
    for (int source_index = 0; source_index < sectors_per_track;
         ++source_index) {
      if (source_order[source_index] == static_cast<uint8_t>(target_sector)) {
        found_index = static_cast<uint8_t>(source_index);
        break;
      }
    }
    sector_order[target_sector] =
        (found_index == iie::sector_not_found) ? 0 : found_index;
  }
}

auto iie_probe(const uint8_t* header_data, size_t header_size,
               uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  if (header_data == nullptr) {
    return disk_probe_no;
  }
  (void)file_size;
  (void)ext_hint;

  if (header_size <= static_cast<size_t>(iie::variant_offset)) {
    return disk_probe_no;
  }

  if (memcmp(header_data, iie::signature.data(), iie::signature_len) == 0 &&
      header_data[iie::variant_offset] <= iie::variant_max_total) {
    return disk_probe_definite;
  }

  return disk_probe_no;
}

// Why: Opens a SimSystem //e disk image and pre-calculates track offsets.
// These images store either raw sectors (legacy) or raw nibbles (modern), so
// offset caching is required for constant-time track seeking.
auto iie_open(const char* path, uint32_t file_offset, uint8_t enhanced_speed,
              bool* out_is_read_only, void** out_instance) -> DiskError_e {
  if (path == nullptr || out_instance == nullptr) {
    return disk_err_io;
  }
  (void)file_offset;
  (void)enhanced_speed;

  auto instance_ptr = std::unique_ptr<IieInstance_t>(new IieInstance_t());

  instance_ptr->file.reset(fopen(path, "r+b"));
  instance_ptr->os_readonly = false;

  if (instance_ptr->file == nullptr) {
    instance_ptr->file.reset(fopen(path, "rb"));
    instance_ptr->os_readonly = true;
  }

  if (instance_ptr->file == nullptr) {
    return disk_err_io;
  }

  if (out_is_read_only != nullptr) {
    *out_is_read_only = instance_ptr->os_readonly;
  }

  if (fseek(instance_ptr->file.get(), 0, SEEK_END) != 0) {
    return disk_err_io;
  }
  const long total_file_size = ftell(instance_ptr->file.get());
  if (total_file_size < static_cast<long>(iie::header_size)) {
    return disk_err_corrupt;
  }
  if (fseek(instance_ptr->file.get(), 0, SEEK_SET) != 0) {
    return disk_err_io;
  }

  if (fread(instance_ptr->header.data(), 1, iie::header_size,
            instance_ptr->file.get()) != iie::header_size) {
    return disk_err_io;
  }

  if (instance_ptr->header[iie::variant_offset] <= iie::variant_max_legacy) {
    iie_convert_sector_order(&instance_ptr->header[iie::sector_map_offset],
                             instance_ptr->sector_order.data());
    for (int t = 0; t < iie::tracks; ++t) {
      instance_ptr->track_offsets.at(static_cast<size_t>(t)) =
          static_cast<uint32_t>(t * dos::track_size + iie::track_data_offset);
      instance_ptr->track_nibble_counts.at(static_cast<size_t>(t)) =
          static_cast<uint16_t>(nibbles_per_track);
    }
  } else {
    uint32_t running_offset = iie::header_size;
    for (int t = 0; t < iie::tracks; ++t) {
      const size_t map_offset =
          static_cast<size_t>(t * phases_per_track) + iie::nibble_map_offset;
      if (map_offset + sizeof(uint16_t) > iie::header_size) {
        return disk_err_corrupt;
      }
      uint16_t nib_count = read_u16_le(&instance_ptr->header.at(map_offset));
      if (nib_count > nibbles_per_track) {
        nib_count = static_cast<uint16_t>(nibbles_per_track);
      }
      instance_ptr->track_offsets.at(static_cast<size_t>(t)) = running_offset;
      instance_ptr->track_nibble_counts.at(static_cast<size_t>(t)) = nib_count;
      running_offset += nib_count;
    }
  }

  *out_instance = reinterpret_cast<void*>(instance_ptr.release());
  return disk_err_none;
}

auto iie_close(void* instance_handle) -> void {
  if (instance_handle == nullptr) {
    return;
  }
  delete reinterpret_cast<IieInstance_t*>(instance_handle);
}

auto iie_is_write_protected(void* instance_handle) -> bool {
  if (instance_handle == nullptr) {
    return true;
  }
  return reinterpret_cast<IieInstance_t*>(instance_handle)->os_readonly;
}

// Why: Fetches track data from the image based on its variant. Legacy images
// require on-the-fly nibblization with custom sector mapping, while modern
// images store raw bitstreams directly.
auto iie_read_track(void* instance_handle, int track, int phase,
                    uint8_t* track_buffer, int* out_nibbles) -> void {
  if (out_nibbles != nullptr) {
    *out_nibbles = 0;
  }

  if (instance_handle == nullptr || track_buffer == nullptr) {
    return;
  }
  (void)phase;
  auto* ii_ptr = reinterpret_cast<IieInstance_t*>(instance_handle);

  if (track < 0 || track >= iie::tracks) {
    return;
  }

  const uint32_t offset = ii_ptr->track_offsets.at(static_cast<size_t>(track));
  const uint16_t nib_count =
      ii_ptr->track_nibble_counts.at(static_cast<size_t>(track));

  if (fseek(ii_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    if (out_nibbles != nullptr) {
      *out_nibbles = 0;
    }
    return;
  }

  if (ii_ptr->header[iie::variant_offset] <= iie::variant_max_legacy) {
    std::fill(ii_ptr->work_buffer.begin(), ii_ptr->work_buffer.end(), 0);
    if (fread(ii_ptr->work_buffer.data(), 1, dos::track_size,
              ii_ptr->file.get()) != dos::track_size) {
      if (out_nibbles != nullptr) {
        *out_nibbles = 0;
      }
      return;
    }
    const uint32_t nibbles = disk_encoding_nibblize_track_custom_order(
        ii_ptr->work_buffer.data(), track_buffer, ii_ptr->sector_order.data(),
        track);
    if (out_nibbles != nullptr) {
      *out_nibbles = static_cast<int>(nibbles);
    }
  } else {
    std::fill_n(track_buffer, nibbles_per_track, 0xFF);
    const size_t read_count =
        fread(track_buffer, 1, nib_count, ii_ptr->file.get());
    if (out_nibbles != nullptr) {
      *out_nibbles = static_cast<int>(read_count);
    }
  }
}

auto iie_command(void* instance_handle, uint32_t cmd_id, const void* payload,
                 size_t payload_size) -> PeripheralStatus_t {
  (void)cmd_id;
  (void)payload;
  (void)payload_size;
  if (instance_handle == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

const char* const g_iie_supported_exts[] = {"iie", nullptr};

}  // namespace

extern "C" const DiskFormatDriver_t g_iie_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = 0,
    .name = "IIE",
    .creatable_exts = nullptr,
    .supported_exts = g_iie_supported_exts,
    .probe = iie_probe,
    .open = iie_open,
    .close = iie_close,
    .is_write_protected = iie_is_write_protected,
    .read_track = iie_read_track,
    .write_track = nullptr,
    .create = nullptr,
    .command = iie_command,
    .read_flux_bit = nullptr};

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory,
// bugprone-easily-swappable-parameters, modernize-make-unique)
