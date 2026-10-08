// SPDX-License-Identifier: GPL-2.0-only
#include "core/Asset.h"

#include <memory>
#include <string>

#include "VideoSurface.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "font.xpm"
#include "splash.xpm"

namespace {

constexpr const char* asset_master_dsk = "Master.dsk";

std::unique_ptr<Assets> assets_storage;
AssetFreeIconFn free_icon_callback = nullptr;

auto asset_find_master_disk(std::string* path_out) -> bool {
  if (path_out == nullptr) {
    return false;
  }

  const std::string full_path = Path::find_data_file(asset_master_dsk);
  if (full_path.empty()) {
    Logger::warning("Could not find %s in any search path\n", asset_master_dsk);
    return false;
  }

  *path_out = full_path;
  Logger::info("Master disk: %s\n", path_out->c_str());
  return true;
}

}  // namespace

Assets* assets = nullptr;

auto asset_set_free_icon_callback(AssetFreeIconFn cb) noexcept -> void {
  free_icon_callback = cb;
}

auto asset_init() -> bool {
  if (assets_storage != nullptr) {
    asset_quit();
  }

  assets_storage.reset(new Assets());
  assets = assets_storage.get();

  assets->font = video_load_xpm(font_xpm);
  if (assets->font == nullptr) {
    asset_quit();
    return false;
  }

  assets->splash = video_load_xpm(splash_xpm);
  if (assets->splash == nullptr) {
    asset_quit();
    return false;
  }

  return true;
}

auto asset_quit() noexcept -> void {
  if (assets == nullptr) {
    return;
  }

  if (free_icon_callback != nullptr) {
    free_icon_callback();
  }

  assets->icon = nullptr;

  if (assets->font != nullptr) {
    video_destroy_surface(assets->font);
    assets->font = nullptr;
  }

  if (assets->splash != nullptr) {
    video_destroy_surface(assets->splash);
    assets->splash = nullptr;
  }

  assets_storage.reset();
  assets = nullptr;
}

auto asset_insert_master_disk() -> int {
  std::string path;
  if (!asset_find_master_disk(&path)) {
    return -1;
  }

  Configuration::instance().set_string(cfg_sec_slots, cfg_disk_image1, path);

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  util_safe_strcpy(cmd.path, path.c_str(), disk_insert_path_max);
  cmd.write_protected = 0;

  return peripheral_command(disk_default_slot, disk_cmd_insert, &cmd,
                            sizeof(cmd));
}
