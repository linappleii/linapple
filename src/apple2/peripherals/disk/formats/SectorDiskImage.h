// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
// Justification: This header defines a C99-compatible ABI for sector-based
// disk image backends, allowing them to be shared across multiple drivers.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "core/Peripheral_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SectorDiskImage_t SectorDiskImage_t;

SectorDiskImage_t* sector_disk_image_open(const char* path,
                                          uint32_t file_offset,
                                          bool is_dos_order,
                                          uint8_t enhanced_speed,
                                          bool* out_is_read_only);

void sector_disk_image_close(SectorDiskImage_t* image_ptr);

bool sector_disk_image_is_write_protected(SectorDiskImage_t* image_ptr);

void sector_disk_image_read_track(SectorDiskImage_t* image_ptr, int track,
                                 uint8_t* track_buffer, int* out_nibbles);

void sector_disk_image_write_track(SectorDiskImage_t* image_ptr, int track,
                                  const uint8_t* track_buffer, int nibbles);

DiskError_e sector_disk_image_create(const char* path);

DiskProbe_e sector_disk_image_probe_signature(const uint8_t* header_data,
                                              size_t header_size,
                                              uint32_t file_size,
                                              bool is_dos_order);

PeripheralStatus sector_disk_image_command(SectorDiskImage_t* image_ptr,
                                           uint32_t cmd_id, const void* payload,
                                           size_t payload_size);


#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
