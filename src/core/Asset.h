// SPDX-License-Identifier: GPL-2.0-only
#pragma once

struct VideoSurface;

struct Assets {
  void* icon{nullptr};
  VideoSurface* font{nullptr};
  VideoSurface* splash{nullptr};
};

extern Assets* assets;

using AssetFreeIconFn = void (*)();
auto asset_set_free_icon_callback(AssetFreeIconFn cb) noexcept -> void;

auto asset_init() -> bool;
auto asset_quit() noexcept -> void;
auto asset_insert_master_disk() -> int;
