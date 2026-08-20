// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-trailing-return-type)
// Justification: This header defines a C99-compatible ABI for disk container
// detection, allowing centralized handling of wrapper formats like MacBinary.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
namespace macbinary {
constexpr size_t header_size = 128;
}

extern "C" {
#endif

uint32_t disk_container_detect_macbinary(const uint8_t* header_data,
                                         size_t header_size,
                                         uint32_t file_size);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-trailing-return-type)
