// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/PoDriver.h"

#include <cstdint>
#include <cstring>

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"
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

// Why: Probes for a ProDOS-ordered disk image by prioritizing physical data
// patterns (Directory signatures) over file extensions.
auto po_probe(const uint8_t* header_data, size_t header_size,
              uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  if (header_data == nullptr) {
    return disk_probe_no;
  }

  const auto sig_probe = sector_disk_image_probe_signature(
      header_data, header_size, file_size, false);

  if (sig_probe == disk_probe_definite) {
    return disk_probe_definite;
  }

  if (ext_hint != nullptr && std::strcmp(ext_hint, ".po") == 0) {
    return disk_probe_possible;
  }

  return sig_probe;
}

auto po_open(const char* path, uint32_t file_offset, uint8_t enhanced_speed,
             bool* out_is_read_only, void** out_instance_handle)
    -> DiskError_e {
  if (path == nullptr || out_instance_handle == nullptr) {
    return disk_err_io;
  }

  auto* image_ptr = sector_disk_image_open(path, file_offset, false,
                                           enhanced_speed, out_is_read_only);
  if (image_ptr == nullptr) {
    return disk_err_io;
  }
  *out_instance_handle = static_cast<void*>(image_ptr);
  return disk_err_none;
}

auto po_close(void* instance_handle) -> void {
  if (instance_handle == nullptr) {
    return;
  }
  sector_disk_image_close(static_cast<SectorDiskImage_t*>(instance_handle));
}

auto po_is_write_protected(void* instance_handle) -> bool {
  if (instance_handle == nullptr) {
    return true;
  }
  return sector_disk_image_is_write_protected(
      static_cast<SectorDiskImage_t*>(instance_handle));
}

auto po_read_track(void* instance_handle, int track, int phase,
                   uint8_t* track_buffer, int* out_nibbles) -> void {
  if (out_nibbles != nullptr) {
    *out_nibbles = 0;
  }

  if (instance_handle == nullptr) {
    return;
  }

  (void)phase;
  sector_disk_image_read_track(static_cast<SectorDiskImage_t*>(instance_handle),
                               track, track_buffer, out_nibbles);
}

auto po_write_track(void* instance_handle, int track, int phase,
                    const uint8_t* track_buffer, int nibbles) -> void {
  if (instance_handle == nullptr) {
    return;
  }
  (void)phase;
  sector_disk_image_write_track(
      static_cast<SectorDiskImage_t*>(instance_handle), track, track_buffer,
      nibbles);
}

auto po_create(const char* path) -> DiskError_e {
  if (path == nullptr) {
    return disk_err_io;
  }
  return sector_disk_image_create(path);
}

auto po_command(void* instance_handle, uint32_t cmd_id, const void* payload,
                size_t payload_size) -> PeripheralStatus_t {
  if (instance_handle == nullptr) {
    return peripheral_error;
  }
  return sector_disk_image_command(
      static_cast<SectorDiskImage_t*>(instance_handle), cmd_id, payload,
      payload_size);
}

const char* const g_po_creatable_exts[] = {".po", nullptr};

}  // namespace

extern "C" const DiskFormatDriver_t g_po_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = disk_driver_cap_write,
    .name = "ProDOS Order",
    .creatable_exts = g_po_creatable_exts,
    .probe = po_probe,
    .open = po_open,
    .close = po_close,
    .is_write_protected = po_is_write_protected,
    .read_track = po_read_track,
    .write_track = po_write_track,
    .create = po_create,
    .command = po_command,
    .read_flux_bit = nullptr
};

// NOLINTEND(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-type-static-cast-downcast,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
