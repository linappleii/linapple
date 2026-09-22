// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// The container code lives in the media shelf; these are the names the disk
// and harddisk loaders, their tests and the disk fuzzer still call. Every
// wrapper forwards and folds the library's error onto the bool those callers
// expect. The file goes once each of them includes ImageContainer.h itself.

#include <cstddef>
#include <cstdint>

#include "apple2/media/image_container/ImageContainer.h"

namespace disk_container {
constexpr size_t floppy_decompression_threshold = 4 * 1024 * 1024;     // 4 MB
constexpr size_t harddisk_decompression_threshold = 32 * 1024 * 1024;  // 32 MB
constexpr size_t compression_ratio_limit = image_container_ratio_limit;
}  // namespace disk_container

inline auto disk_container_detect_macbinary(const uint8_t* header_data,
                                            size_t header_len,
                                            uint32_t file_size) -> uint32_t {
  return image_container_detect_macbinary(header_data, header_len, file_size);
}

inline auto disk_container_prepare_compressed_path(
    const char* image_path, char* out_load_path, size_t max_path_len,
    size_t uncompressed_threshold, bool* out_is_temporary) -> bool {
  return image_container_prepare_compressed_path(
             image_path, out_load_path, max_path_len, uncompressed_threshold,
             out_is_temporary) == image_container_ok;
}

inline auto disk_container_payload_name(const char* image_path, char* out_name,
                                        size_t max_name_len) -> bool {
  return image_container_payload_name(image_path, out_name, max_name_len) ==
         image_container_ok;
}

inline auto disk_container_supported_extensions(void) -> const char* const* {
  return image_container_supported_extensions();
}
