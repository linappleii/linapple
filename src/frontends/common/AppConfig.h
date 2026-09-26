// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/Registry.h"

inline auto app_config_default(AppConfig_t* config) -> void {
  if (config != nullptr) {
    *config = AppConfig_t{};
  }
}
