// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification:
// This header defines a language-neutral C ABI for the GCR nibblization engine.
// C system headers, typedefs, and C-style arrays are required for compatibility
// with C-based consumers.

#include <stdbool.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  disk_encoding_encode_table_size = 64,
  disk_encoding_decode_table_size = 128,
  disk_encoding_sector_data_size = 342,
  disk_encoding_sector_with_checksum_size = 343,
  disk_encoding_work_buffer_offset = 0x1000,
  disk_encoding_checksum_buffer_offset = 0x1400,
  disk_encoding_gap3_size = 16
};

enum { disk_encoding_work_buffer_size = 0x3000 };

auto disk_encoding_nibblize_track(uint8_t* work_buffer,
                                  uint8_t* track_image_buffer,
                                  bool is_dos_order, int track) -> uint32_t;

auto disk_encoding_nibblize_track_custom_order(uint8_t* work_buffer,
                                               uint8_t* track_image_buffer,
                                               const uint8_t* sector_order,
                                               int track) -> uint32_t;

auto disk_encoding_denibblize_track(uint8_t* work_buffer, uint8_t* track_image,
                                    bool is_dos_order, int nibbles) -> void;

/* Lay a nibble track down as cells, eight to a nibble. */
DiskError_e disk_encoding_nibbles_to_bits(const uint8_t* nibbles,
                                          uint32_t count, uint8_t* bits,
                                          uint32_t max_bits,
                                          uint32_t* out_bit_count);

/* Read cells back the way the shift register does: discard cells until one
   carries a pulse, then take the eight that follow. */
DiskError_e disk_encoding_bits_to_nibbles(const uint8_t* bits,
                                          uint32_t bit_count, uint8_t* nibbles,
                                          uint32_t max_nibbles,
                                          uint32_t* out_count);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
