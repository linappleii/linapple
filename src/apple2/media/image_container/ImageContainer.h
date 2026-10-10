// SPDX-License-Identifier: GPL-2.0-only
#pragma once

/* NOLINTBEGIN(modernize-deprecated-headers, modernize-use-trailing-return-type,
   modernize-use-using) */
/* Justification: C99 ABI shared across cards.
   Reentrancy: Stateless and thread-safe. Temporary files use prefix linapple_
   under $TMPDIR (or /tmp), mode 0600; caller unlinks them. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  image_container_ok = 0,
  image_container_invalid_argument = 1,
  image_container_io = 2,
  image_container_corrupt = 3,
  image_container_too_large = 4,
  image_container_not_found = 5
} ImageContainerError;

enum {
  image_container_macbinary_header_len = 128,
  image_container_ratio_limit = 100
};

uint32_t image_container_detect_macbinary(const uint8_t* header_data,
                                          size_t header_len,
                                          uint32_t file_size);

ImageContainerError image_container_prepare_compressed_path(
    const char* image_path, char* out_load_path, size_t max_path_len,
    size_t uncompressed_threshold, bool* out_is_temporary);

ImageContainerError image_container_payload_name(const char* image_path,
                                                   char* out_name,
                                                   size_t max_name_len);

const char* const* image_container_supported_extensions(void);

#ifdef __cplusplus
}
#endif

/* NOLINTEND(modernize-deprecated-headers, modernize-use-trailing-return-type,
   modernize-use-using) */
