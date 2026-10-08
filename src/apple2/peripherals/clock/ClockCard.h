// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-use-trailing-return-type, readability-identifier-naming)
// Justification: C99-compatible public descriptor export.

struct Peripheral_t;

#ifdef __cplusplus
extern "C" {
#endif

struct Peripheral_t* clockcard_get_descriptor(void);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-use-trailing-return-type, readability-identifier-naming)
