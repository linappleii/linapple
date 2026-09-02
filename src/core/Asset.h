// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/Video.h"

using Assets_t = struct AssetsTag_t {
  void* icon;  // Platform-specific icon handle
  VideoSurface_t* font;
  VideoSurface_t* splash;
};

using assets_t = Assets_t;

extern Assets_t* assets;

using AssetFreeIconFn_t = void (*)();
auto asset_set_free_icon_callback(AssetFreeIconFn_t cb) -> void;

auto asset_init() -> bool;
auto asset_quit() -> void;
auto asset_insert_master_disk() -> int;
