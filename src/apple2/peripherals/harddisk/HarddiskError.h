// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using)
// Justification: a language-neutral C ABI for C consumers.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  harddisk_err_none = 0,
  harddisk_err_io = 1,
  harddisk_err_not_found = 2,
  harddisk_err_read_only = 3,
  harddisk_err_invalid_format = 4
} HarddiskError_e;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using)
