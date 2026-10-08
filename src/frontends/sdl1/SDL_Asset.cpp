// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl1/SDL_Asset.h"

#include <SDL/SDL_error.h>
#include <SDL/SDL_video.h>

#include <cstdio>
#include <string>

#include "core/Asset.h"
#include "core/Util_Path.h"
#include "frontends/sdl1/SdlPtr.h"

auto sdl_asset_load_bmp(const char* filename) -> SdlSurfacePtr {
  if (filename == nullptr) {
    return nullptr;
  }

  std::string full_path = Path::find_data_file(filename);
  if (full_path.empty()) {
    fprintf(stderr, "asset_load_bmp: Couldn't find %s in any search path!\n",
            filename);
    return nullptr;
  }

  SdlSurfacePtr surf(SDL_LoadBMP(full_path.c_str()));
  if (surf == nullptr) {
    fprintf(stderr, "asset_load_bmp: Failed to load %s from %s: %s\n", filename,
            full_path.c_str(), SDL_GetError());
    return nullptr;
  }

  fprintf(stderr, "asset_load_bmp: Loaded %s from %s\n", filename,
          full_path.c_str());
  return surf;
}

namespace {
static SdlSurfacePtr s_app_icon;
}  // namespace

auto sdl_asset_free_icon() -> void {
  s_app_icon.reset();
  if (assets != nullptr) {
    assets->icon = nullptr;
  }
}

auto sdl_asset_load_icon() -> void {
  if (assets == nullptr) {
    return;
  }

  sdl_asset_free_icon();
  asset_set_free_icon_callback(sdl_asset_free_icon);
  s_app_icon = sdl_asset_load_bmp("icon.bmp");
  assets->icon = static_cast<void*>(s_app_icon.get());
}
