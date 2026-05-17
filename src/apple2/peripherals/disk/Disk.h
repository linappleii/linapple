// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/Peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-use-using)
// Justification: Identity header for the Disk II module.

auto Disk_GetDescriptor() -> Peripheral_t*;

// NOLINTEND(modernize-use-using)

#ifdef __cplusplus
}
#endif
