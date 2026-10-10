// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
// Justification: a C99-compatible ABI for the 2IMG header parser, shaped so it
// can serve another card unchanged.

#include <stdbool.h>
#include <stdint.h>

#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { two_img_header_size = 64 };

/* The 2IMG image format field: 0 DOS 3.3 order, 1 ProDOS order, 2 nibble. */
enum {
  two_img_format_dos = 0,
  two_img_format_prodos = 1,
  two_img_format_nibble = 2,
};

/* What the parser settled from a 2IMG header: where the blocks start, how
   many there are, in which order, and whether the image is locked. */
typedef struct {
  uint32_t image_format;
  uint32_t data_offset;
  uint32_t data_length;
  uint32_t block_count;
  bool locked;
} TwoImgHeader;

/* Reads the 64-byte header of a file of file_size bytes. Refuses a magic
   other than 2IMG, a header size under 64 or past the file, a data offset
   inside the header or past the file, a ProDOS image whose block count and
   data length both are given and disagree, a data length that is not whole
   blocks, a DOS-order payload of any size but 143,360 bytes and any chunk
   that does not lie whole between the data and the end of the file, each
   with harddisk_err_invalid_format; a nibble image with
   harddisk_err_not_block_image. The version field is read and ignored. */
HarddiskError two_img_parse(const uint8_t* header, uint64_t file_size,
                              TwoImgHeader* out);

extern const HarddiskFormatDriver two_img_driver;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
