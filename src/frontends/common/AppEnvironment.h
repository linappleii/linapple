// SPDX-License-Identifier: GPL-2.0-only
#pragma once

struct Configuration;
using AppConfig = Configuration;
auto app_env_resolve_paths(AppConfig* config) -> void;
