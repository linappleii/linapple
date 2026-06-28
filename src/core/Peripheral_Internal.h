// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

auto Peripheral_Register_Internal() -> void;
auto Peripheral_Plugins_Init() -> void;
auto Peripheral_Plugins_Shutdown() -> void;
auto Peripheral_Find_Internal(const char* name) -> Peripheral_t*;
auto Peripheral_GetPluginPath(const char* name) -> const char*;
auto Peripheral_IsAnyActive() -> bool;

#ifdef __cplusplus
}
#endif
