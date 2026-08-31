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

uint32_t disk_container_detect_macbinary(const uint8_t* header_data,
                                         size_t header_size,
                                         uint32_t file_size);

bool disk_container_prepare_compressed_path(const char* image_path,
                                            char* out_load_path,
                                            size_t max_path_len,
                                            size_t uncompressed_threshold,
                                            bool* out_is_temporary);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-trailing-return-type)
