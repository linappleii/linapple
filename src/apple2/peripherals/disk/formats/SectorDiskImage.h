// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// Justification: This header defines a C99-compatible ABI for sector-based disk image backends, allowing them to be shared across multiple drivers.
// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/Peripheral_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SectorDiskImage_t SectorDiskImage_t;

SectorDiskImage_t* sector_disk_image_open(const char* path,
                                          uint32_t file_offset,
                                          bool is_dos_order, bool read_only);

void sector_disk_image_close(SectorDiskImage_t* image_ptr);

bool sector_disk_image_is_write_protected(SectorDiskImage_t* image_ptr);

DiskError_e sector_disk_image_read_track_bits(SectorDiskImage_t* image_ptr,
                                              uint32_t quarter_track,
                                              uint8_t* bits, uint32_t max_bits,
                                              uint32_t* out_bit_count);

DiskError_e sector_disk_image_write_track_bits(SectorDiskImage_t* image_ptr,
                                               uint32_t quarter_track,
                                               const uint8_t* bits,
                                               uint32_t bit_count);

DiskError_e sector_disk_image_create(const char* path);

DiskProbe_e sector_disk_image_probe_signature(const uint8_t* header_data,
                                              size_t header_size,
                                              uint32_t file_size,
                                              bool is_dos_order);

PeripheralStatus_t sector_disk_image_command(SectorDiskImage_t* image_ptr,
                                             uint32_t cmd_id,
                                             const void* payload,
                                             size_t payload_size);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
