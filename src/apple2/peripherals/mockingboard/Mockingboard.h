// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-use-trailing-return-type, readability-identifier-naming)
// Justification: C99-compatible public descriptor export.

#ifdef __cplusplus
extern "C" {
#endif

struct Peripheral;

auto mockingboard_get_descriptor() -> struct Peripheral*;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-use-trailing-return-type, readability-identifier-naming)
