// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/NibDriver.h"

#include <cstdint>
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/DiskFormatRegistration.h"
#include "apple2/peripherals/disk/formats/BitstreamDiskImage.h"

// Justification: Format drivers utilize a procedural C-compatible handle system
// and standardized probing signatures mandated by the Disk subsystem ABI.
// Array-to-pointer decay and C-style arrays are required for driver descriptor
// registration.
// NOLINTBEGIN(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-type-static-cast-downcast, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

namespace {
namespace physical {
constexpr int tracks = 35;
constexpr int disk_size = tracks * static_cast<int>(nibbles_per_track);
}  // namespace physical

auto nib_probe(const uint8_t* header_data, size_t header_size,
               uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  if (header_data == nullptr) {
    return disk_probe_no;
  }
  (void)header_size;
  (void)ext_hint;

  if (file_size == static_cast<uint32_t>(physical::disk_size)) {
    return disk_probe_definite;
  }

  return disk_probe_no;
}

auto nib_open(const char* path, uint32_t file_offset, bool read_only,
              void** out_instance) -> DiskError_e {
  if (path == nullptr || out_instance == nullptr) {
    return disk_err_io;
  }
  auto* image_ptr = bitstream_disk_image_open(
      path, file_offset, nibbles_per_track, read_only);
  if (image_ptr == nullptr) {
    return disk_err_io;
  }
  *out_instance = static_cast<void*>(image_ptr);
  return disk_err_none;
}

auto nib_close(void* instance_handle) -> void {
  if (instance_handle == nullptr) {
    return;
  }
  bitstream_disk_image_close(
      static_cast<BitstreamDiskImage_t*>(instance_handle));
}

auto nib_is_write_protected(void* instance_handle) -> bool {
  if (instance_handle == nullptr) {
    return true;
  }
  return bitstream_disk_image_is_write_protected(
      static_cast<BitstreamDiskImage_t*>(instance_handle));
}

auto nib_read_track_bits(void* instance_handle, uint32_t quarter_track,
                         uint8_t* bits, uint32_t max_bits,
                         uint32_t* out_bit_count) -> DiskError_e {
  if (instance_handle == nullptr) {
    return disk_err_invalid_argument;
  }
  return bitstream_disk_image_read_track_bits(
      static_cast<BitstreamDiskImage_t*>(instance_handle), quarter_track, bits,
      max_bits, out_bit_count);
}

auto nib_write_track_bits(void* instance_handle, uint32_t quarter_track,
                          const uint8_t* bits, uint32_t bit_count)
    -> DiskError_e {
  if (instance_handle == nullptr) {
    return disk_err_invalid_argument;
  }
  return bitstream_disk_image_write_track_bits(
      static_cast<BitstreamDiskImage_t*>(instance_handle), quarter_track, bits,
      bit_count);
}

auto nib_create(const char* path) -> DiskError_e {
  if (path == nullptr) {
    return disk_err_io;
  }
  return bitstream_disk_image_create(
      path, static_cast<uint32_t>(physical::disk_size));
}

const char* const g_nib_supported_exts[] = {"nib", nullptr};
}  // namespace

extern "C" const DiskFormatDriver_t g_nib_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = disk_driver_cap_write,
    .name = "NIB (6656-nibble)",
    .supported_exts = g_nib_supported_exts,
    .probe = nib_probe,
    .open = nib_open,
    .close = nib_close,
    .is_write_protected = nib_is_write_protected,
    .read_track_bits = nib_read_track_bits,
    .write_track_bits = nib_write_track_bits,
    .create = nib_create};

static const DiskFormatRegistration_t k_reg{&g_nib_driver};

// NOLINTEND(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-type-static-cast-downcast, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
