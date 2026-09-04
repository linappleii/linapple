// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "frontends/sdl1/SdlPtr.h"

auto sdl_asset_load_bmp(const char* filename) -> SdlSurfacePtr_t;
void sdl_asset_load_icon();
void sdl_asset_free_icon();
