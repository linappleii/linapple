// SPDX-License-Identifier: GPL-2.0-only
#include "core/Asset.h"

// Core asset and resource manager for font and splash surfaces
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, misc-include-cleaner,
// cppcoreguidelines-pro-type-cstyle-cast,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-init-variables)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "font.xpm"
#include "splash.xpm"

static constexpr const char* asset_master_dsk = "Master.dsk";

static std::unique_ptr<Assets_t> assets_ptr;
Assets_t* assets = nullptr;
static AssetFreeIconFn_t s_free_icon_cb = nullptr;

auto asset_set_free_icon_callback(AssetFreeIconFn_t cb) -> void {
  s_free_icon_cb = cb;
}

auto asset_init() -> bool {
  if (assets_ptr != nullptr) {
    asset_quit();
  }

  assets_ptr.reset(new Assets_t());
  assets = assets_ptr.get();
  assets->icon = nullptr;

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

auto asset_quit() -> void {
  if (assets != nullptr) {
    if (s_free_icon_cb != nullptr) {
      s_free_icon_cb();
    }

    if (assets->font != nullptr) {
      video_destroy_surface(assets->font);
      assets->font = nullptr;
    }

    if (assets->splash != nullptr) {
      video_destroy_surface(assets->splash);
      assets->splash = nullptr;
    }

    assets_ptr.reset();
    assets = nullptr;
  }
}

static auto asset_find_master_disk(char* path_out, size_t max_len) -> int {
  if (path_out == nullptr || max_len == 0) {
    return 255;
  }

  std::string full_path = Path::find_data_file(asset_master_dsk);
  if (full_path.empty()) {
    Logger::warning("Could not find %s in any search path\n", asset_master_dsk);
    return 255;
  }

  Util_SafeStrCpy(path_out, full_path.c_str(), max_len);
  Logger::info("Master disk: %s\n", path_out);
  return 0;
}

auto asset_insert_master_disk() -> int {
  char path[path_max_len]{};

  int err = asset_find_master_disk(path, sizeof(path));
  if (err != 0) {
    return 255;
  }

  Configuration_t::instance().set_string("Slots", REGVALUE_DISK_IMAGE1, path);

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  Util_SafeStrCpy(cmd.path, path, disk_insert_path_max);
  cmd.write_protected = 0;
  cmd.create_if_necessary = 0;

  peripheral_command(disk_default_slot, disk_cmd_insert, &cmd, sizeof(cmd));

  return 0;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, misc-include-cleaner,
// cppcoreguidelines-pro-type-cstyle-cast,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-init-variables)
