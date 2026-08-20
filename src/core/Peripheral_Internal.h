// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

auto peripheral_register_internal() -> void;
auto peripheral_plugins_init() -> void;
auto peripheral_plugins_shutdown() -> void;
auto peripheral_find_internal(const char* name) -> Peripheral_t*;
auto peripheral_get_plugin_path(const char* name) -> const char*;
auto peripheral_is_any_active() -> bool;

#ifdef __cplusplus
}
#endif
