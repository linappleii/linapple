// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
// modernize-use-trailing-return-type) Justification: This header defines a
// language-neutral C ABI. C system headers, typedefs, and C-style return types
// are required for compatibility with C-based consumers.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  disk_err_none = 0,
  disk_err_file_not_found = 1,
  disk_err_unsupported_format = 2,
  disk_err_corrupt = 3,
  disk_err_write_protected = 4,
  disk_err_out_of_memory = 5,
  disk_err_io = 6
} DiskError_e;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
// modernize-use-trailing-return-type)
