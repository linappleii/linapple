// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// Justification: This header defines a C99-compatible ABI for block-based disk image backends, allowing shared I/O and container handling.
// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlockDiskImage_t BlockDiskImage_t;

BlockDiskImage_t* block_disk_image_open(const char* path, uint32_t file_offset,
                                        bool* out_is_read_only);

void block_disk_image_close(BlockDiskImage_t* image_ptr);

bool block_disk_image_is_write_protected(BlockDiskImage_t* image_ptr);

void block_disk_image_set_write_protected(BlockDiskImage_t* image_ptr,
                                          bool write_protected);

HarddiskError_e block_disk_image_read_block(BlockDiskImage_t* image_ptr,
                                            uint32_t block_num,
                                            uint8_t* buffer);

HarddiskError_e block_disk_image_write_block(BlockDiskImage_t* image_ptr,
                                             uint32_t block_num,
                                             const uint8_t* buffer);

uint32_t block_disk_image_get_total_blocks(BlockDiskImage_t* image_ptr);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
