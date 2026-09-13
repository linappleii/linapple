// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Peripheral_t;

auto mockingboard_get_descriptor() -> struct Peripheral_t*;

#ifdef __cplusplus
}
#endif
