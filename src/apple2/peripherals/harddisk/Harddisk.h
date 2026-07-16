// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/Peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

auto harddisk_get_descriptor() -> Peripheral_t*;

#ifdef __cplusplus
}
#endif
