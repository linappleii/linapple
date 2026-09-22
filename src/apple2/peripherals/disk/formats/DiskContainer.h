// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-trailing-return-type)
// Justification: This header defines a C99-compatible ABI for disk container
// detection and decompression, allowing centralized handling of wrapper
// formats like MacBinary and compression archives like gzip/zip.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
namespace macbinary {
constexpr size_t header_size = 128;
}

namespace disk_container {
constexpr size_t floppy_decompression_threshold = 4 * 1024 * 1024;     // 4 MB
constexpr size_t harddisk_decompression_threshold = 32 * 1024 * 1024;  // 32 MB
// Extraction is refused once the output passes the caller's threshold AND
// exceeds this many times the archive's size. The threshold lets a zero-filled
// blank of any plausible image size through; past it the ratio bounds the
// output at 100 x the archive. That product is the deliberate ceiling on what
// one archive can put in $TMPDIR; there is no further cap.
constexpr size_t compression_ratio_limit = 100;
}  // namespace disk_container

extern "C" {
#endif

/* The number of bytes a MacBinary II or III wrapper occupies before the image:
   128 when the first header_size bytes of the file carry one whose CRC-16
   checks (the MacBinary II standard's own test), else 0. MacBinary I has no
   CRC and is not recognised. header_size must be at least 128. */
uint32_t disk_container_detect_macbinary(const uint8_t* header_data,
                                         size_t header_size,
                                         uint32_t file_size);

/* Resolves image_path to a file a driver can fopen. A plain path is copied
   through with *out_is_temporary false. A .gz or .zip (by suffix, case
   insensitive; a zip's first file entry, directory entries and __MACOSX/
   AppleDouble sidecars skipped) is extracted to $TMPDIR/linapple_XXXXXX,
   mode 0600, with *out_is_temporary true; the caller owns that file, must
   unlink it, and can never write back through it. Both out-params are set at
   entry, so on false *out_is_temporary is false and out_load_path is empty.
   A path that does not fit max_path_len is refused, never truncated. False
   for a null argument, an unwritable temporary, a broken archive or one with
   no file entry, an output past the bound at compression_ratio_limit, or a
   temporary that could not be closed. */
bool disk_container_prepare_compressed_path(const char* image_path,
                                            char* out_load_path,
                                            size_t max_path_len,
                                            size_t uncompressed_threshold,
                                            bool* out_is_temporary);

/* The name the image carries inside its container, or the path's own
   basename when it is not in one. Only the extension is of interest: a
   game.dsk.gz holds a .dsk, and probing it as a .gz asks every format driver
   a question none of them answer. A zip names its first file entry, directory
   entries and __MACOSX/ AppleDouble sidecars skipped; a gz drops its suffix.
   A name that does not fit max_name_len is refused, never truncated. */
bool disk_container_payload_name(const char* image_path, char* out_name,
                                 size_t max_name_len);

/* The archive extensions this layer can unwrap, without the dot, as a
   NULL-terminated list. A file browser offering disk images has to offer
   these too, and only this layer knows what it can open. */
const char* const* disk_container_supported_extensions(void);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-trailing-return-type)
