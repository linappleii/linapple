// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/config/ConfigSchema.h"
#include "frontends/common/AppConfig.h"

/**
 * Resolve application paths and initialize core services (Logger, Registry,
 * Modern Configuration).
 */
auto app_env_resolve_paths(AppConfig_t* config) -> void;

/**
 * Retrieve the active modern configuration structure.
 */
auto app_env_get_config() -> const LinAppleConfig_t&;
auto app_env_get_config_mut() -> LinAppleConfig_t&;
