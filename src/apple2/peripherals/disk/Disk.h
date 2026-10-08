// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-use-trailing-return-type, readability-identifier-naming)
// Justification: C99-compatible public descriptor export.

#ifdef __cplusplus
extern "C" {
#endif

struct Peripheral_t;

struct Peripheral_t* disk_get_descriptor(void);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-use-trailing-return-type, readability-identifier-naming)
