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

// Replaces the host clock behind HostInterface_t::GetLocalTime. The core's
// own implementation reads the wall clock, which no test can pin, so the
// harness installs a provider that answers with a frozen value; production
// never installs one. Passing nullptr restores the wall clock.
typedef bool (*LocalTimeProvider_t)(void* ctx, HostLocalTime_t* out);
auto linapple_set_local_time_provider(LocalTimeProvider_t provider, void* ctx)
    -> void;

#ifdef __cplusplus
}
#endif
