// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/DoDriver.h"

#include <cstdint>
#include <cstring>

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"
#include "core/Peripheral_Types.h"

// NOLINTBEGIN(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-type-static-cast-downcast, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification: Format drivers utilize a procedural C-compatible handle system
// and standardized probing signatures mandated by the Disk subsystem ABI.
// Array-to-pointer decay and C-style arrays are required for driver descriptor
// registration.

namespace {

// Why: Probes for a DOS-ordered disk image by prioritizing physical data
// patterns (VTOC signatures) over file extensions.
auto do_probe(const uint8_t* header_data, size_t header_size,
              uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  const auto sig_probe = sector_disk_image_probe_signature(
      header_data, header_size, file_size, true);

  if (sig_probe == disk_probe_definite) {
    return disk_probe_definite;
  }

  if (ext_hint != nullptr && (std::strcmp(ext_hint, ".do") == 0 ||
                              std::strcmp(ext_hint, ".dsk") == 0)) {
    return disk_probe_possible;
  }

  return sig_probe;
}

auto do_open(const char* path, uint32_t file_offset, uint8_t enhanced_speed,
             bool* out_is_read_only, void** out_instance) -> DiskError_e {
  if (path == nullptr || out_instance == nullptr) {
    return disk_err_io;
  }

  auto* image_ptr = sector_disk_image_open(path, file_offset, true,
                                           enhanced_speed, out_is_read_only);
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

auto do_read_track(void* instance_handle, int track, int phase,
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

auto do_write_track(void* instance, int track, int phase,
                    const uint8_t* track_buffer, int nibbles) -> void {
  if (instance == nullptr) {
    return;
  }
  (void)phase;
  sector_disk_image_write_track(static_cast<SectorDiskImage_t*>(instance),
                                track, track_buffer, nibbles);
}

auto do_create(const char* path) -> DiskError_e {
  if (path == nullptr) {
    return disk_err_io;
  }
  return sector_disk_image_create(path);
}

auto do_command(void* instance, uint32_t cmd_id, const void* payload,
                size_t payload_size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  return sector_disk_image_command(static_cast<SectorDiskImage_t*>(instance),
                                   cmd_id, payload, payload_size);
}

const char* const g_do_creatable_exts[] = {".do", ".dsk", nullptr};

}  // namespace

extern "C" const DiskFormatDriver_t g_do_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = disk_driver_cap_write,
    .name = "DOS Order",
    .creatable_exts = g_do_creatable_exts,
    .probe = do_probe,
    .open = do_open,
    .close = do_close,
    .is_write_protected = do_is_write_protected,
    .read_track = do_read_track,
    .write_track = do_write_track,
    .create = do_create,
    .command = do_command,
    .read_flux_bit = nullptr
};

// NOLINTEND(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-type-static-cast-downcast, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
