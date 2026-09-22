// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using)
// Justification: a C99-compatible ABI for the nibble-image backends (NIB,
// NB2), so they share I/O and container handling.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NibbleDiskImage_t NibbleDiskImage_t;

/* The largest image the family describes: thirty-five whole tracks of the
   longest slot. A file with more behind its offset is refused before anything
   is allocated for it. */
enum {
  nibble_image_tracks = 35,
  nibble_image_max_bytes = nibble_image_tracks * nibbles_per_track
};

/* Opens the image at file_offset within path, its tracks track_nibbles bytes
   each, and hands the instance back through out_instance, which is nulled
   first. A null argument, or a track_nibbles of zero or above
   nibbles_per_track, is disk_err_invalid_argument; a path that does not exist
   disk_err_file_not_found; any other failure to open or measure the file
   disk_err_io; a file shorter than file_offset, or with nothing recorded past
   it, disk_err_corrupt; one with more than nibble_image_max_bytes past it
   disk_err_unsupported. */
DiskError_e nibble_disk_image_open(const char* path, uint32_t file_offset,
                                   uint32_t track_nibbles, bool read_only,
                                   void** out_instance);

/* These four carry the DiskFormatDriver_t signatures, instance being the
   pointer open handed out, so a driver descriptor names them directly. Open
   and create stay with the driver because NIB and NB2 differ only in the
   track size they pass here. */
void nibble_disk_image_close(void* instance);

bool nibble_disk_image_is_write_protected(void* instance);

DiskError_e nibble_disk_image_read_track_bits(void* instance,
                                              uint32_t quarter_track,
                                              uint8_t* bits, uint32_t max_bits,
                                              uint32_t* out_bit_count,
                                              uint8_t* out_bit_timing);

DiskError_e nibble_disk_image_write_track_bits(void* instance,
                                               uint32_t quarter_track,
                                               const uint8_t* bits,
                                               uint32_t bit_count);

DiskError_e nibble_disk_image_create(const char* path, uint32_t total_size);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using)
