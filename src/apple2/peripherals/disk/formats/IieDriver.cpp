// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/IieDriver.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/DiskFormatRegistration.h"
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"
#include "core/Util_Endian.h"
#include "core/Util_Path.h"

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification:
// This module uses procedural patterns for C-compatibility. google-runtime-int
// is required for fseek offsets. owning-memory and make-unique are suppressed
// for C++11 compatibility and handle-based resource management.
// easily-swappable-parameters is mandated by the Disk Driver ABI signatures.

// A SimSystem //e image, read the way AppleWin's CIIeImage reads one, which
// is the behaviour reference for this driver. The header is 88 bytes: the
// signature, a variant byte at 13 and a per-track map from 14. A variant of
// 0..2 is the legacy layout, where track t is sixteen 256-byte sectors at
// 30 + 4096t and the sixteen bytes at 14 name the physical slot of each file
// sector, so track 0 begins inside the header. A variant of 3 is the nibble
// layout, where the nibbles run from 88 with track t's little-endian count at
// 14 + 2t and each track beginning where the one before it ends.
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
constexpr size_t header_map_stride = 2;

constexpr uint8_t variant_max_legacy = 2;
constexpr uint8_t variant_max_total = 3;
constexpr uint8_t sector_not_found = 0xFF;

// Compile-time guarantee: The SimSystem //e header map offsets for all tracks
// must strictly reside within the fixed header size, independent of runtime
// disk geometry or stepper phase constants.
static_assert(nibble_map_offset + ((tracks - 1) * header_map_stride) +
                      sizeof(uint16_t) <=
                  static_cast<size_t>(header_size),
              "IIE header track nibble map exceeds allocated header size");
static_assert(sector_map_offset + sectors_per_track <=
                  static_cast<size_t>(header_size),
              "IIE header sector map exceeds allocated header size");
}  // namespace iie

struct IieInstance_t {
  FilePtr_t file{nullptr, fclose};
  std::array<uint8_t, iie::header_size> header{};
  std::array<uint8_t, sectors_per_track> sector_order{};
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  std::array<uint8_t, nibbles_per_track> nibbles{};
  std::array<uint8_t, nibbles_per_track> sync_mask{};
  std::array<uint8_t, sector_image_track_bytes> sectors{};
  std::array<uint32_t, iie::tracks> track_offsets{};
  std::array<uint16_t, iie::tracks> track_nibble_counts{};
  bool host_read_only = false;

  IieInstance_t() = default;
  ~IieInstance_t() = default;

  IieInstance_t(const IieInstance_t&) = delete;
  auto operator=(const IieInstance_t&) -> IieInstance_t& = delete;
  IieInstance_t(IieInstance_t&&) = default;
  auto operator=(IieInstance_t&&) -> IieInstance_t& = default;
};

// The header map runs from file sector to physical slot; the nibblizer's
// table runs the other way, so the map is inverted once here.
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

// Every track offset is resolved at open: in the nibble layout each track
// starts where the one before ends, so a bad count anywhere shifts every later
// track, and such an image is refused here rather than misread on a seek.
auto iie_open(const char* path, uint32_t file_offset, bool read_only,
              void** out_instance) -> DiskError_e {
  if (out_instance == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_instance = nullptr;
  if (path == nullptr) {
    return disk_err_invalid_argument;
  }

  auto instance_ptr = std::unique_ptr<IieInstance_t>(new IieInstance_t());

  instance_ptr->host_read_only = read_only;
  if (!read_only) {
    instance_ptr->file.reset(fopen(path, "r+b"));
  }

  if (instance_ptr->file == nullptr) {
    instance_ptr->file.reset(fopen(path, "rb"));
    instance_ptr->host_read_only = true;
  }

  if (instance_ptr->file == nullptr) {
    return (errno == ENOENT) ? disk_err_file_not_found : disk_err_io;
  }

  const int64_t total_file_size = Path::file_size(instance_ptr->file.get());
  if (total_file_size < static_cast<int64_t>(file_offset) +
                            static_cast<int64_t>(iie::header_size)) {
    return disk_err_corrupt;
  }

  if (fseek(instance_ptr->file.get(), static_cast<long>(file_offset),
            SEEK_SET) != 0) {
    return disk_err_io;
  }

  if (fread(instance_ptr->header.data(), 1, iie::header_size,
            instance_ptr->file.get()) != iie::header_size) {
    return disk_err_io;
  }

  if (instance_ptr->header[iie::variant_offset] > iie::variant_max_total) {
    return disk_err_unsupported_format;
  }

  if (instance_ptr->header[iie::variant_offset] <= iie::variant_max_legacy) {
    iie_convert_sector_order(&instance_ptr->header[iie::sector_map_offset],
                             instance_ptr->sector_order.data());
    for (int t = 0; t < iie::tracks; ++t) {
      const uint32_t offset =
          file_offset + static_cast<uint32_t>(t * sector_image_track_bytes +
                                              iie::track_data_offset);
      if (static_cast<int64_t>(offset) + sector_image_track_bytes >
          total_file_size) {
        return disk_err_corrupt;
      }
      instance_ptr->track_offsets[static_cast<size_t>(t)] = offset;
      instance_ptr->track_nibble_counts[static_cast<size_t>(t)] =
          static_cast<uint16_t>(nibbles_per_track);
    }
  } else {
    uint32_t running_offset = file_offset + iie::header_size;
    for (int t = 0; t < iie::tracks; ++t) {
      const size_t map_offset =
          (static_cast<size_t>(t) * iie::header_map_stride) +
          static_cast<size_t>(iie::nibble_map_offset);
      // A count the slot buffer cannot hold would put every later track at
      // the wrong offset if it were clamped, so the image is refused.
      const uint16_t nib_count = read_u16_le(&instance_ptr->header[map_offset]);
      if (nib_count > nibbles_per_track) {
        return disk_err_corrupt;
      }
      if (static_cast<int64_t>(running_offset) + nib_count > total_file_size) {
        return disk_err_corrupt;
      }
      instance_ptr->track_offsets[static_cast<size_t>(t)] = running_offset;
      instance_ptr->track_nibble_counts[static_cast<size_t>(t)] = nib_count;
      running_offset += nib_count;
    }
  }

  *out_instance = reinterpret_cast<void*>(instance_ptr.release());
  return disk_err_none;
}

auto iie_close(void* instance_handle) -> void {
  delete reinterpret_cast<IieInstance_t*>(instance_handle);
}

auto iie_is_write_protected(void* instance_handle) -> bool {
  if (instance_handle == nullptr) {
    return true;
  }
  return reinterpret_cast<IieInstance_t*>(instance_handle)->host_read_only;
}

auto iie_read_track_bits(void* instance_handle, uint32_t quarter_track,
                         uint8_t* bits, uint32_t max_bits,
                         uint32_t* out_bit_count, uint8_t* out_bit_timing)
    -> DiskError_e {
  if (instance_handle == nullptr || bits == nullptr ||
      out_bit_count == nullptr || out_bit_timing == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_bit_count = 0;
  *out_bit_timing = disk_default_bit_timing;

  auto* ii_ptr = reinterpret_cast<IieInstance_t*>(instance_handle);

  // The image holds 35 tracks; past them the surface is blank, as a WOZ
  // treats an unrecorded track.
  const uint32_t track = quarter_track / quarter_tracks_per_cylinder;
  if (track >= static_cast<uint32_t>(iie::tracks)) {
    return disk_err_none;
  }

  const uint32_t offset = ii_ptr->track_offsets[track];
  const uint16_t nib_count = ii_ptr->track_nibble_counts[track];

  if (fseek(ii_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return disk_err_io;
  }

  if (ii_ptr->header[iie::variant_offset] <= iie::variant_max_legacy) {
    if (fread(ii_ptr->sectors.data(), 1, sector_image_track_bytes,
              ii_ptr->file.get()) != sector_image_track_bytes) {
      return disk_err_io;
    }
    uint32_t nibbles_read = 0;
    const DiskError_e synthesised = disk_encoding_nibblize_track(
        ii_ptr->sector_order.data(), track, ii_ptr->sectors.data(),
        ii_ptr->nibbles.data(), ii_ptr->sync_mask.data(), &nibbles_read,
        ii_ptr->scratch.data());
    if (synthesised != disk_err_none) {
      return synthesised;
    }
    return disk_encoding_nibbles_to_bits(ii_ptr->nibbles.data(), nibbles_read,
                                         ii_ptr->sync_mask.data(), bits,
                                         max_bits, out_bit_count);
  }

  // The modern variant stores raw nibbles with no record of which were
  // written as sync, so the adapter reads the track's own shape.
  const uint32_t nibbles_read = static_cast<uint32_t>(
      fread(ii_ptr->nibbles.data(), 1, nib_count, ii_ptr->file.get()));
  return disk_encoding_nibbles_to_bits(ii_ptr->nibbles.data(), nibbles_read,
                                       nullptr, bits, max_bits, out_bit_count);
}

const char* const iie_supported_exts[] = {"iie", nullptr};

}  // namespace

extern "C" const DiskFormatDriver_t g_iie_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = 0,
    .name = "IIE",
    .supported_exts = iie_supported_exts,
    .probe = iie_probe,
    .open = iie_open,
    .close = iie_close,
    .is_write_protected = iie_is_write_protected,
    .read_track_bits = iie_read_track_bits,
    .write_track_bits = nullptr,
    .create = nullptr};

static const DiskFormatRegistration_t registration{&g_iie_driver};

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
