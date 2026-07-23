// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/NibDriver.h"

#include <cstdint>
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/BitstreamDiskImage.h"
#include "core/Peripheral_Types.h"

// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-type-static-cast-downcast,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays) Justification:
// Format drivers utilize a procedural C-compatible handle system and
// standardized probing signatures mandated by the Disk subsystem ABI.
// Array-to-pointer decay and C-style arrays are required for driver descriptor
// registration.

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

auto nib_open(const char* path, uint32_t file_offset, uint8_t enhanced_speed,
              bool* out_is_read_only, void** out_instance) -> DiskError_e {
  if (path == nullptr || out_instance == nullptr) {
    return disk_err_io;
  }
  (void)enhanced_speed;
  auto* image_ptr = bitstream_disk_image_open(
      path, file_offset, nibbles_per_track, out_is_read_only);
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

auto nib_read_track(void* instance_handle, int track, int phase,
                    uint8_t* track_buffer, int* out_nibbles) -> void {
  if (out_nibbles != nullptr) {
    *out_nibbles = 0;
  }

  if (instance_handle == nullptr) {
    return;
  }

  (void)phase;
  bitstream_disk_image_read_track(
      static_cast<BitstreamDiskImage_t*>(instance_handle), track, track_buffer,
      out_nibbles);
}

auto nib_write_track(void* instance_handle, int track, int phase,
                     const uint8_t* track_buffer, int nibbles) -> void {
  if (instance_handle == nullptr) {
    return;
  }
  (void)phase;
  bitstream_disk_image_write_track(
      static_cast<BitstreamDiskImage_t*>(instance_handle), track, track_buffer,
      nibbles);
}

auto nib_create(const char* path) -> DiskError_e {
  if (path == nullptr) {
    return disk_err_io;
  }
  return bitstream_disk_image_create(
      path, static_cast<uint32_t>(physical::disk_size));
}

auto nib_command(void* instance_handle, uint32_t cmd_id, const void* payload,
                 size_t payload_size) -> PeripheralStatus_t {
  (void)cmd_id;
  (void)payload;
  (void)payload_size;
  if (instance_handle == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

const char* const g_nib_creatable_exts[] = {".nib", nullptr};
}  // namespace

extern "C" const DiskFormatDriver_t g_nib_driver = {
    .AbiVersion_t = disk_format_abi_version,
    .capabilities = disk_driver_cap_write,
    .name = "NIB (6656-nibble)",
    .creatable_exts = g_nib_creatable_exts,
    .probe = nib_probe,
    .open = nib_open,
    .close = nib_close,
    .is_write_protected = nib_is_write_protected,
    .read_track = nib_read_track,
    .write_track = nib_write_track,
    .create = nib_create,
    .command = nib_command,
    .read_flux_bit = nullptr
};

// NOLINTEND(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-type-static-cast-downcast,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
