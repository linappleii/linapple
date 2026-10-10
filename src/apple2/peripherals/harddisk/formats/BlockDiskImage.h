// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
// Justification: a C99-compatible ABI for the block-image backend that the
// DOS-order, ProDOS-order and 2MG drivers share.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How the file serializes the disk's blocks. A ProDOS-order file holds block
   b at b x 512; a DOS-order file holds the sixteen DOS 3.3 sectors of each
   track in order, and a block is the pair of sectors Fig. 3.14 of Beneath
   Apple ProDOS gives for it, so DOS order exists only for the 280 blocks of a
   5.25-inch disk. */
typedef enum {
  block_disk_order_prodos = 0,
  block_disk_order_dos = 1,
} BlockDiskOrder;

/* The one size a DOS-order image has: 35 tracks of sixteen 256-byte sectors,
   which is 280 blocks. */
enum { block_disk_image_dos_size = 143360, block_disk_image_dos_blocks = 280 };

/* Opens the image at file_offset within path and hands the instance back
   through out_instance, which is nulled first. block_count names the blocks
   the file serves; 0 takes every whole block from file_offset to the end. A
   null argument is harddisk_err_io, a path that does not exist
   harddisk_err_not_found, a file too short for the blocks claimed, a DOS-order
   image that is not 280 blocks, or an image of no block at all
   harddisk_err_invalid_format. read_only is folded into the protection
   answer with the host's own refusal to open the file for writing. */
HarddiskError block_disk_image_open(const char* path, uint32_t file_offset,
                                      BlockDiskOrder order,
                                      uint32_t block_count, bool read_only,
                                      void** out_instance);

/* These five carry the HarddiskFormatDriver signatures, instance being the
   pointer open handed out, so a driver descriptor names them directly and the
   format-specific code in a driver is its probe and its open. A failure to
   sync the file on close is recorded through harddisk_loader_note. */
void block_disk_image_close(void* instance);

bool block_disk_image_is_write_protected(void* instance);

HarddiskError block_disk_image_read_block(void* instance, uint32_t block_num,
                                            uint8_t* buffer);

HarddiskError block_disk_image_write_block(void* instance, uint32_t block_num,
                                             const uint8_t* buffer);

uint32_t block_disk_image_get_total_blocks(void* instance);

/* The content check the two order drivers share: definite when a DOS 3.3
   catalog or a ProDOS volume directory reads coherently in the order asked,
   possible for any other file of the DOS-order size, no for every other
   size. header_size bounds every read, so a short header answers possible
   rather than reading past it. */
HarddiskProbe block_disk_image_probe_signature(const uint8_t* header_data,
                                                 size_t header_size,
                                                 uint64_t file_size,
                                                 BlockDiskOrder order);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
