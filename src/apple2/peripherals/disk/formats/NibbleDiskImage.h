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
#include "apple2/peripherals/disk/DiskFormatDriver.h"

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

/* The probe NIB and NB2 share, each passing the length of its whole image
   and its own dotted lowercase extension. A WOZ signature at the front is
   disk_probe_no whatever the length: a WOZ can be exactly as long as a nibble
   image and only its magic tells them apart. Otherwise a file_size other than
   image_bytes is disk_probe_no, a matching ext_hint disk_probe_definite, and
   the length alone disk_probe_possible. A null header_data is disk_probe_no
   for a direct caller; the loader never passes one. */
DiskProbe_e nibble_disk_image_probe(const uint8_t* header_data,
                                    size_t header_size, uint32_t file_size,
                                    const char* ext_hint, uint32_t image_bytes,
                                    const char* ext);

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

/* Writes a formatted blank at path: nibble_image_tracks tracks of sixteen
   zero sectors, each track padded with 0xFF to track_nibbles bytes. The path
   must not exist yet (disk_err_io if it does); a track_nibbles of zero or
   above nibbles_per_track is disk_err_invalid_argument, and one too short for
   the sixteen sectors disk_err_unsupported. A file the call could not finish
   is removed. */
DiskError_e nibble_disk_image_create(const char* path, uint32_t track_nibbles);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using)
