// SPDX-License-Identifier: GPL-2.0-only
#pragma once

struct Configuration_t;
using AppConfig_t = Configuration_t;

// Resolves application configuration paths and initializes core services
// (Logger, Registry).
auto app_env_resolve_paths(AppConfig_t* config) -> void;
