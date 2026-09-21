// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// modernize-use-trailing-return-type) Justification: This header defines a
// C99-compatible ABI for nibble-image backends (NIB, NB2),
// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using)
// allowing shared I/O and container handling.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NibbleDiskImage_t NibbleDiskImage_t;

NibbleDiskImage_t* nibble_disk_image_open(const char* path,
                                          uint32_t file_offset,
                                          uint32_t nibbles_per_track,
                                          bool read_only);

void nibble_disk_image_close(NibbleDiskImage_t* image_ptr);

bool nibble_disk_image_is_write_protected(NibbleDiskImage_t* image_ptr);

DiskError_e nibble_disk_image_read_track_bits(NibbleDiskImage_t* image_ptr,
                                              uint32_t quarter_track,
                                              uint8_t* bits, uint32_t max_bits,
                                              uint32_t* out_bit_count,
                                              uint8_t* out_bit_timing);

DiskError_e nibble_disk_image_write_track_bits(NibbleDiskImage_t* image_ptr,
                                               uint32_t quarter_track,
                                               const uint8_t* bits,
                                               uint32_t bit_count);

DiskError_e nibble_disk_image_create(const char* path, uint32_t total_size);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using)
