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
constexpr size_t compression_ratio_limit = 100;  // 100:1 ratio
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

bool disk_container_prepare_compressed_path(const char* image_path,
                                            char* out_load_path,
                                            size_t max_path_len,
                                            size_t uncompressed_threshold,
                                            bool* out_is_temporary);

/* The name the image carries inside its container, or the path's own
   basename when it is not in one. Only the extension is of interest: a
   game.dsk.gz holds a .dsk, and probing it as a .gz asks every format driver
   a question none of them answer. */
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
