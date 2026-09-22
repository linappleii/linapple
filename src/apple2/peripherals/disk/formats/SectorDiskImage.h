// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// Justification: This header defines a C99-compatible ABI for sector-based disk image backends, allowing them to be shared across multiple drivers.
// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SectorDiskImage_t SectorDiskImage_t;

/* The largest file the sector family opens: a 35-track image of 143,360 bytes
   behind a 128-byte wrapper. Every size the probe admits is that image with
   up to 255 bytes missing from its last sector or up to 128 following it, and
   a file above this is disk_err_unsupported before anything is allocated. */
enum { sector_image_max_bytes = 143488 };

/* Opens the image at file_offset within path and hands the instance back
   through out_instance, which is nulled first. A null argument is
   disk_err_invalid_argument, a path that does not exist
   disk_err_file_not_found, any other failure to open or measure the file
   disk_err_io, a file above sector_image_max_bytes disk_err_unsupported and a
   file of any other size the probe would not admit disk_err_corrupt. The
   first 143,360 bytes are the image: bytes after them are never read or
   written, and bytes the last track lacks read as zero until that track is
   written back, which completes it. */
DiskError_e sector_disk_image_open(const char* path, uint32_t file_offset,
                                   bool is_dos_order, bool read_only,
                                   void** out_instance);

/* These five carry the DiskFormatDriver_t signatures, instance being the
   pointer open handed out, so a driver descriptor names them directly and the
   order-specific code in a driver is its probe and its open. */
void sector_disk_image_close(void* instance);

bool sector_disk_image_is_write_protected(void* instance);

DiskError_e sector_disk_image_read_track_bits(void* instance,
                                              uint32_t quarter_track,
                                              uint8_t* bits, uint32_t max_bits,
                                              uint32_t* out_bit_count,
                                              uint8_t* out_bit_timing);

DiskError_e sector_disk_image_write_track_bits(void* instance,
                                               uint32_t quarter_track,
                                               const uint8_t* bits,
                                               uint32_t bit_count);

DiskError_e sector_disk_image_create(const char* path);

DiskProbe_e sector_disk_image_probe_signature(const uint8_t* header_data,
                                              size_t header_size,
                                              uint32_t file_size,
                                              bool is_dos_order);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
