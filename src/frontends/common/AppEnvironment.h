#pragma once

#include "frontends/common/AppConfig.h"

/**
 * Resolve application paths and initialize core services (Logger, Registry).
 */
void AppEnv_ResolvePaths(AppConfig_t* config);
