// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
// Justification: This header defines a C99-compatible ABI for bitstream-based
// disk image backends (NIB, NB2), allowing shared I/O and container handling.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BitstreamDiskImage_t BitstreamDiskImage_t;

BitstreamDiskImage_t* bitstream_disk_image_open(const char* path,
                                                uint32_t file_offset,
                                                uint32_t nibbles_per_track,
                                                bool* out_is_read_only);

void bitstream_disk_image_close(BitstreamDiskImage_t* image_ptr);

bool bitstream_disk_image_is_write_protected(BitstreamDiskImage_t* image_ptr);

void bitstream_disk_image_read_track(BitstreamDiskImage_t* image_ptr, int track,
                                     uint8_t* track_buffer, int* out_nibbles);

void bitstream_disk_image_write_track(BitstreamDiskImage_t* image_ptr,
                                      int track, const uint8_t* track_buffer,
                                      int nibbles);

DiskError_e bitstream_disk_image_create(const char* path, uint32_t total_size);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
