// SPDX-License-Identifier: GPL-2.0-only
#pragma once

/* NOLINTBEGIN(modernize-deprecated-headers, modernize-use-trailing-return-type,
   modernize-use-using) */
/* Justification: this header is a C99 ABI. A disk image often arrives inside a
   container: a MacBinary wrapper (128 bytes prepended to the image, handled in
   place through an offset) or a compression archive (.gz or .zip, extracted to
   a temporary file the caller owns). Every card that opens images shares this
   one implementation, so nothing here may depend on any card.

   Reentrancy: the library keeps no state between calls, so any thread may
   call any function at any time. Temporary files are created under $TMPDIR
   (or /tmp) with the prefix linapple_, mode 0600; the caller unlinks them. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  image_container_ok = 0,
  /* A null pointer, a zero-length buffer, or a path or name that does not
     fit the buffer given: refused rather than truncated, because a shortened
     path names a different file and a shortened name can lose the extension
     that picks the format driver. */
  image_container_invalid_argument = 1,
  /* The host failed: the archive could not be read, or the temporary could
     not be created, written or closed. */
  image_container_io = 2,
  /* The archive is not what its suffix says, is damaged, holds no file entry,
     or holds one this build cannot decode. */
  image_container_corrupt = 3,
  /* The extracted output passed the bound below. */
  image_container_too_large = 4,
  /* The archive named by image_path does not exist. */
  image_container_not_found = 5
} ImageContainerError_e;

enum {
  /* The wrapper a MacBinary II or III file puts before the data fork. */
  image_container_macbinary_header_len = 128,
  /* Extraction is refused once the output passes the caller's threshold AND
     exceeds this many times the archive's size. The threshold lets a
     zero-filled blank of any plausible image size through; past it the ratio
     bounds the output at 100 x the archive. That product is the deliberate
     ceiling on what one archive can put in $TMPDIR; there is no further cap.
     The threshold itself is the caller's: a floppy card and a hard-disk card
     accept very different image sizes, and this library holds no policy. */
  image_container_ratio_limit = 100
};

/* The number of bytes a MacBinary II or III wrapper occupies before the
   image: image_container_macbinary_header_len when the first header_len bytes
   of the file carry one whose CRC-16 checks (the MacBinary II standard's own
   test), else 0. MacBinary I has no CRC and is not recognised. header_len
   must be at least image_container_macbinary_header_len and file_size must
   be larger than it, or the answer is 0. A null header_data answers 0. */
uint32_t image_container_detect_macbinary(const uint8_t* header_data,
                                          size_t header_len,
                                          uint32_t file_size);

/* Resolves image_path to a file a driver can fopen. A plain path is copied
   through with *out_is_temporary false. A .gz or .zip (by suffix, case
   insensitive, never by magic) is extracted to $TMPDIR/linapple_XXXXXX with
   *out_is_temporary true; the caller owns that file, must unlink it, and can
   never write back through it. A .gz is the whole stream, and a stream that
   is not gzip passes through byte for byte, as zlib's gzread does. A .zip is
   its first file entry, skipping directory entries and __MACOSX/ AppleDouble
   sidecars. Both out-params are set at entry, so on any error
   *out_is_temporary is false and out_load_path is empty. Errors:
   invalid_argument (null argument, zero max_path_len, a path or temporary
   template that does not fit), not_found (no such archive), io (archive
   unreadable, temporary not creatable, writable or closable), corrupt (not a
   valid archive, no file entry, undecodable entry, truncated stream) and
   too_large (the bound at image_container_ratio_limit). */
ImageContainerError_e image_container_prepare_compressed_path(
    const char* image_path, char* out_load_path, size_t max_path_len,
    size_t uncompressed_threshold, bool* out_is_temporary);

/* The name the image carries inside its container, or the path's own
   basename when it is not in one. Only the extension is of interest: a
   game.dsk.gz holds a .dsk, and probing it as a .gz asks every format driver
   a question none of them answer. A zip names its first file entry (the same
   choice as extraction), without any directory prefix; a gz drops its suffix
   without opening the stream, so the gzip FNAME field is never consulted.
   Errors: invalid_argument (null argument, zero max_name_len, a name that
   does not fit), and for a zip the same not_found, io and corrupt answers as
   extraction. */
ImageContainerError_e image_container_payload_name(const char* image_path,
                                                   char* out_name,
                                                   size_t max_name_len);

/* The archive extensions this layer can unwrap, lowercase, without the dot,
   as a NULL-terminated list in a fixed order. A file browser offering disk
   images has to offer these too, and only this layer knows what it can open. */
const char* const* image_container_supported_extensions(void);

#ifdef __cplusplus
}
#endif

/* NOLINTEND(modernize-deprecated-headers, modernize-use-trailing-return-type,
   modernize-use-using) */
