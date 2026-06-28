// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/Video.h"

using Assets_t = struct AssetsTag_t {
  void* icon;  // Platform-specific icon handle
  VideoSurface* font;
  VideoSurface* splash;
};

using assets_t = Assets_t;

extern Assets_t* assets;

auto asset_init() -> bool;
auto asset_quit() -> void;
auto asset_insert_master_disk() -> int;

#ifdef __cplusplus
static inline auto Asset_Init() -> bool { return asset_init(); }
static inline auto Asset_Quit() -> void { asset_quit(); }
static inline auto Asset_InsertMasterDisk() -> int {
  return asset_insert_master_disk();
}
#endif
