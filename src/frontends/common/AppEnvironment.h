// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "frontends/common/AppConfig.h"

/**
 * Resolve application paths and initialize core services (Logger, Registry).
 */
void app_env_resolve_paths(AppConfig_t* config);
