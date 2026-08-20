// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
// modernize-use-trailing-return-type, google-runtime-int) Justification: This
// header defines the C99-compatible public ABI for Harddisk format drivers.
// C-style return types, typedefs, and integer types are required for
// cross-language compatibility with C-based consumers.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { harddisk_format_abi_version = 0 };

typedef enum {
  harddisk_driver_cap_none = 0x00,
  harddisk_driver_cap_write = 0x01
} HarddiskDriverCap_e;

typedef enum {
  harddisk_probe_no = 0,
  harddisk_probe_possible = 1,
  harddisk_probe_definite = 2
} HarddiskProbe_e;

typedef enum {
  harddisk_err_none = 0,
  harddisk_err_io = 1,
  harddisk_err_not_found = 2,
  harddisk_err_read_only = 3,
  harddisk_err_invalid_format = 4
} HarddiskError_e;

typedef struct HarddiskFormatDriver_t {
  int abi_version;
  uint32_t capabilities;
  const char* name;
  const char* const* creatable_exts;

  HarddiskProbe_e (*probe)(const uint8_t* header_data, size_t header_size,
                           uint32_t file_size, const char* ext_hint);

  HarddiskError_e (*open)(const char* path, uint32_t file_offset,
                          bool* out_os_readonly, void** out_instance_handle);

  void (*close)(void* instance_handle);

  bool (*is_write_protected)(void* instance_handle);

  HarddiskError_e (*read_block)(void* instance_handle, uint32_t block_num,
                                uint8_t* buffer);

  HarddiskError_e (*write_block)(void* instance_handle, uint32_t block_num,
                                 const uint8_t* buffer);

  uint32_t (*get_total_blocks)(void* instance_handle);
} HarddiskFormatDriver_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
// modernize-use-trailing-return-type, google-runtime-int)
