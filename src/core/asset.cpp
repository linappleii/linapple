#include "core/Common.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>

#include "core/asset.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Registry.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/Video.h"

#include "font.xpm"
#include "splash.xpm"

#define ASSET_MASTER_DSK "Master.dsk"

static std::unique_ptr<assets_t, void (*)(void *)> assets_ptr(nullptr, free);
assets_t *assets = nullptr;

auto Asset_Init() -> bool {
  assets_ptr.reset(static_cast<assets_t *>(calloc(1, sizeof(assets_t))));
  if (!assets_ptr) {
    return false;
  }
  assets = assets_ptr.get();

  // Icon is loaded by the frontend and assigned to assets->icon

  assets->font = VideoLoadXPM(font_xpm);
  if (nullptr == assets->font) {
    return false;
  }

  assets->splash = VideoLoadXPM(splash_xpm);
  return nullptr != assets->splash;
}

void Asset_Quit() {
  if (nullptr != assets) {
    // Icon is freed by the frontend

    if (nullptr != assets->font) {
      VideoDestroySurface(assets->font);
      assets->font = nullptr;
    }

    if (nullptr != assets->splash) {
      VideoDestroySurface(assets->splash);
      assets->splash = nullptr;
    }

    assets_ptr.reset();
    assets = nullptr;
  }
}

auto Asset_FindMasterDisk(char *path_out) -> int {
  std::string fullPath = Path::FindDataFile(ASSET_MASTER_DSK);
  if (fullPath.empty()) {
    printf("[warn ] could not find %s in any search path\n", ASSET_MASTER_DSK);
    return 255;
  }

  Util_SafeStrCpy(path_out, fullPath.c_str(), PATH_MAX_LEN);
  printf("[info ] Master disk: %s\n", path_out);
  return 0;
}

auto Asset_InsertMasterDisk() -> int {
  std::unique_ptr<char, void (*)(void *)> path(static_cast<char *>(malloc(PATH_MAX_LEN)), free);

  int err = Asset_FindMasterDisk(path.get());
  if (err) {
    return 255;
  }

  // 1. Write to registry for persistence and startup loading
  Configuration::Instance().SetString("Slots", REGVALUE_DISK_IMAGE1, path.get());

  // 2. Send immediate command for runtime effect if peripherals are already running
  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  Util_SafeStrCpy(cmd.path, path.get(), disk_insert_path_max);
  cmd.write_protected = 0;
  cmd.create_if_necessary = 0;

  Peripheral_Command(disk_default_slot, disk_cmd_insert, &cmd, sizeof(cmd));

  return 0;
}
