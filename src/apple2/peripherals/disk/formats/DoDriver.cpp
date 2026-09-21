// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/DoDriver.h"

#include <strings.h>

#include <cstdint>
#include <cstring>

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/DiskFormatRegistration.h"
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"

// Justification: Format drivers utilize a procedural C-compatible handle system
// and standardized probing signatures mandated by the Disk subsystem ABI.
// Array-to-pointer decay and C-style arrays are required for driver descriptor
// registration.
// NOLINTBEGIN(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-type-static-cast-downcast, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

namespace {

auto do_probe(const uint8_t* header_data, size_t header_size,
              uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  if (header_data == nullptr) {
    return disk_probe_no;
  }

  const auto sig_probe = sector_disk_image_probe_signature(
      header_data, header_size, file_size, true);

  if (sig_probe == disk_probe_definite) {
    return disk_probe_definite;
  }

  if (ext_hint != nullptr) {
    if (strcasecmp(ext_hint, ".do") == 0 || strcasecmp(ext_hint, ".dsk") == 0) {
      return (sig_probe != disk_probe_no) ? disk_probe_possible : disk_probe_no;
    }
    if (strcasecmp(ext_hint, ".po") == 0) {
      return disk_probe_no;
    }
  }

  return sig_probe;
}

auto do_open(const char* path, uint32_t file_offset, bool read_only,
             void** out_instance) -> DiskError_e {
  if (path == nullptr || out_instance == nullptr) {
    return disk_err_io;
  }

  auto* image_ptr =
      sector_disk_image_open(path, file_offset, true, read_only);
  if (image_ptr == nullptr) {
    return disk_err_io;
  }
  *out_instance = static_cast<void*>(image_ptr);
  return disk_err_none;
}

auto do_close(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  sector_disk_image_close(static_cast<SectorDiskImage_t*>(instance));
}

auto do_is_write_protected(void* instance) -> bool {
  if (instance == nullptr) {
    return true;
  }
  return sector_disk_image_is_write_protected(
      static_cast<SectorDiskImage_t*>(instance));
}

auto do_read_track_bits(void* instance_handle, uint32_t quarter_track,
                        uint8_t* bits, uint32_t max_bits,
                        uint32_t* out_bit_count, uint8_t* out_bit_timing)
    -> DiskError_e {
  if (instance_handle == nullptr) {
    return disk_err_invalid_argument;
  }
  return sector_disk_image_read_track_bits(
      static_cast<SectorDiskImage_t*>(instance_handle), quarter_track, bits,
      max_bits, out_bit_count, out_bit_timing);
}

auto do_write_track_bits(void* instance, uint32_t quarter_track,
                         const uint8_t* bits, uint32_t bit_count)
    -> DiskError_e {
  if (instance == nullptr) {
    return disk_err_invalid_argument;
  }
  return sector_disk_image_write_track_bits(
      static_cast<SectorDiskImage_t*>(instance), quarter_track, bits,
      bit_count);
}

auto do_create(const char* path) -> DiskError_e {
  if (path == nullptr) {
    return disk_err_io;
  }
  return sector_disk_image_create(path);
}

const char* const g_do_supported_exts[] = {"do", "dsk", nullptr};

}  // namespace

extern "C" const DiskFormatDriver_t g_do_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = disk_driver_cap_write,
    .name = "DOS Order",
    .supported_exts = g_do_supported_exts,
    .probe = do_probe,
    .open = do_open,
    .close = do_close,
    .is_write_protected = do_is_write_protected,
    .read_track_bits = do_read_track_bits,
    .write_track_bits = do_write_track_bits,
    .create = do_create};

static const DiskFormatRegistration_t k_reg{&g_do_driver};

// NOLINTEND(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-type-static-cast-downcast, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
