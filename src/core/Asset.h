// SPDX-License-Identifier: GPL-2.0-only
#pragma once

struct VideoSurface_t;

struct Assets_t {
  void* icon{nullptr};
  VideoSurface_t* font{nullptr};
  VideoSurface_t* splash{nullptr};
};

extern Assets_t* assets;

using AssetFreeIconFn_t = void (*)();
auto asset_set_free_icon_callback(AssetFreeIconFn_t cb) noexcept -> void;

auto asset_init() -> bool;
auto asset_quit() noexcept -> void;
auto asset_insert_master_disk() -> int;
