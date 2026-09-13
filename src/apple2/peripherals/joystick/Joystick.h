// SPDX-License-Identifier: GPL-2.0-only
#pragma once

struct Peripheral_t;

#ifdef __cplusplus
extern "C" {
#endif

auto joystick_get_descriptor() -> Peripheral_t*;

#ifdef __cplusplus
}
#endif
