// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/Peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns the descriptor for the Mouse peripheral.
 */
auto Mouse_GetDescriptor() -> Peripheral_t*;

#ifdef __cplusplus
}
#endif
