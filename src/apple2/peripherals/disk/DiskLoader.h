// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/peripherals/disk/DiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-use-using)
// Justification: This header defines a language-neutral C ABI for the disk
// image loader.

auto disk_loader_init() -> void;
auto disk_loader_shutdown() -> void;
auto disk_loader_register(DiskFormatDriver_t* driver) -> void;

auto disk_loader_open(const char* image_path, bool create_if_necessary,
                      uint8_t enhanced_speed, bool* out_is_read_only,
                      DiskFormatDriver_t** out_driver, void** out_instance)
    -> DiskError_e;

auto disk_loader_get_supported_extensions(char* out_buffer, size_t buffer_size)
    -> void;

// NOLINTEND(modernize-use-using)

#ifdef __cplusplus
}
#endif
