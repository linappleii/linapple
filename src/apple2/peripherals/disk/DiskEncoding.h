// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using)
// Justification:
// This header defines a language-neutral C ABI for the GCR nibblization
// engine. C system headers and typedefs are required for compatibility with
// C-based consumers.

#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Working storage the codec needs beside the caller's own track buffers. */
enum { disk_encoding_scratch_size = 0x1800 };

typedef enum {
  disk_sector_order_dos = 0,
  disk_sector_order_prodos = 1
} DiskSectorOrder_e;

/* The physical slot each of the sixteen logical sectors occupies. */
const uint8_t* disk_encoding_sector_order(DiskSectorOrder_e order);

/* Synthesise one track: gap 1, then sixteen address and data fields with
   their own gaps. Writes *out_count nibbles into nibbles_out and, when
   sync_mask_out is given, one byte per nibble marking the gaps. */
DiskError_e disk_encoding_nibblize_track(const uint8_t* sector_order,
                                         uint32_t track,
                                         const uint8_t* sectors_in,
                                         uint8_t* nibbles_out,
                                         uint8_t* sync_mask_out,
                                         uint32_t* out_count, uint8_t* scratch);

/* Read a track back into sixteen sectors. Answers disk_err_corrupt for a
   bad prologue, epilogue, address field or data checksum, and for a track
   that is short of a sector; sectors_out is written only on success. */
DiskError_e disk_encoding_denibblize_track(const uint8_t* sector_order,
                                           uint32_t track,
                                           const uint8_t* nibbles_in,
                                           uint32_t count, uint8_t* sectors_out,
                                           uint8_t* scratch);

/* Lay a nibble track down as cells: eight to a data nibble, ten to a
   self-sync one. sync_mask marks the self-sync nibbles, one byte each; pass
   it null for an image that records no sync information and the run of 0xFF
   ending at a 0xD5 prologue is read as the gap it must be. */
DiskError_e disk_encoding_nibbles_to_bits(const uint8_t* nibbles,
                                          uint32_t count,
                                          const uint8_t* sync_mask,
                                          uint8_t* bits, uint32_t max_bits,
                                          uint32_t* out_bit_count);

/* Read cells back the way the shift register does: discard cells until one
   carries a pulse, then take it and the seven that follow as the byte.
   Answers disk_err_unsupported, with the first max_nibbles decoded and
   counted, when a further nibble remains once the buffer is full. */
DiskError_e disk_encoding_bits_to_nibbles(const uint8_t* bits,
                                          uint32_t bit_count, uint8_t* nibbles,
                                          uint32_t max_nibbles,
                                          uint32_t* out_count);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using)
