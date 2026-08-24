// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
// modernize-use-trailing-return-type, google-runtime-int) Justification: This
// header defines a C99-compatible ABI for the harddisk loading subsystem.
// C-style types and function signatures are required for cross-language
// compatibility.

#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

void harddisk_loader_init(void);
void harddisk_loader_shutdown(void);
auto harddisk_loader_register(HarddiskFormatDriver_t* driver_ptr) -> void;

HarddiskError_e harddisk_loader_open(const char* path, bool* out_os_readonly,
                                     HarddiskFormatDriver_t** out_driver,
                                     void** out_instance_handle);

void harddisk_loader_get_supported_extensions(char* out_buffer,
                                              size_t buffer_size);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
// modernize-use-trailing-return-type, google-runtime-int)
