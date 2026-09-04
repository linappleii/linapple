// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "frontends/sdl2/SdlPtr.h"

auto asset_load_bmp(const char* filename) -> SdlSurfacePtr_t;
auto sdl_asset_free_icon() -> void;
auto sdl_asset_load_icon() -> void;
