// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/Nb2Driver.h"

#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/DiskFormatRegistration.h"
#include "apple2/peripherals/disk/formats/NibbleDiskImage.h"

// Justification: Format drivers utilize a procedural C-compatible handle system
// and standardized probing signatures mandated by the Disk subsystem ABI.
// Array-to-pointer decay and C-style arrays are required for driver descriptor
// registration.


namespace {
namespace nb2 {
constexpr uint32_t track_size = 6384;
constexpr uint32_t disk_size = nibble_image_tracks * track_size;
}  // namespace nb2

auto nb2_probe(const uint8_t* header_data, size_t header_size,
               uint32_t file_size, const char* ext_hint) -> DiskProbe {
  return nibble_disk_image_probe(header_data, header_size, file_size, ext_hint,
                                 nb2::disk_size, ".nb2");
}

auto nb2_open(const char* path, uint32_t file_offset, bool read_only,
              void** out_instance) -> DiskError {
  return nibble_disk_image_open(path, file_offset, nb2::track_size, read_only,
                                out_instance);
}

auto nb2_create(const char* path) -> DiskError {
  return nibble_disk_image_create(path, nb2::track_size);
}

const char* const nb2_supported_exts[] = {"nb2", nullptr};
}  // namespace

extern "C" const DiskFormatDriver nb2_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = disk_driver_cap_write | disk_driver_cap_create,
    .name = "NB2 (6384-nibble)",
    .supported_exts = nb2_supported_exts,
    .probe = nb2_probe,
    .open = nb2_open,
    .close = nibble_disk_image_close,
    .is_write_protected = nibble_disk_image_is_write_protected,
    .read_track_bits = nibble_disk_image_read_track_bits,
    .write_track_bits = nibble_disk_image_write_track_bits,
    .create = nb2_create,
};

static const DiskFormatRegistration registration{&nb2_driver};


