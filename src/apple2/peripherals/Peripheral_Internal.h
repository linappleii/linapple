// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/peripherals/Peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

auto peripheral_register_internal() -> void;
auto peripheral_plugins_init(const char* plugin_dir = nullptr) -> void;
auto peripheral_plugins_shutdown() -> void;
auto peripheral_find_internal(const char* name) -> Peripheral_t*;
auto peripheral_get_plugin_path(const char* name) -> const char*;
auto peripheral_is_any_active() -> bool;

// Test hook: inject frozen host clock provider.
typedef bool (*LocalTimeProvider_t)(void* ctx, HostLocalTime_t* out);
auto linapple_set_local_time_provider(LocalTimeProvider_t provider, void* ctx)
    -> void;

#ifdef __cplusplus
}
#endif
