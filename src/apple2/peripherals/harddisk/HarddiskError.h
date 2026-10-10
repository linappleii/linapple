// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-use-using)
// Justification: a language-neutral C ABI for C consumers.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  harddisk_err_none = 0,
  harddisk_err_io = 1,
  harddisk_err_not_found = 2,
  harddisk_err_read_only = 3,
  harddisk_err_invalid_format = 4,
  /* A nibble or flux image holds no blocks a block device could serve. */
  harddisk_err_not_block_image = 5,
} HarddiskError;

/* The codes a ProDOS block device returns in A with the carry set (ProDOS 8
   Technical Reference Manual, 6.3.2), which the MLI passes through to the
   program (4.8). */
typedef enum {
  harddisk_prodos_ok = 0x00,
  harddisk_prodos_io_error = 0x27,
  harddisk_prodos_no_device = 0x28,
  harddisk_prodos_write_protected = 0x2B,
} HarddiskProdosError;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-use-using)
