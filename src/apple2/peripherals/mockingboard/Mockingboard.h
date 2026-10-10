// SPDX-License-Identifier: GPL-2.0-only
#pragma once


#ifdef __cplusplus
extern "C" {
#endif

struct Peripheral;

auto mockingboard_get_descriptor() -> struct Peripheral*;

#ifdef __cplusplus
}
#endif

