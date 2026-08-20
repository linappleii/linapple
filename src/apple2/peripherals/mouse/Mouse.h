// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "../../../core/Peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-use-using)
// Justification: Identity header for the Mouse module.

auto mouse_get_descriptor() -> Peripheral_t*;

// NOLINTEND(modernize-use-using)

#ifdef __cplusplus
}
#endif
