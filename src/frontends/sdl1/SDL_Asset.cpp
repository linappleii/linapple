// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl1/SDL_Asset.h"

#include <SDL/SDL_video.h>

#include <cstdio>
#include <string>

#include "core/Asset.h"
#include "core/Util_Path.h"

auto sdl_asset_load_bmp(const char* filename) -> SdlSurfacePtr_t {
  std::string fullPath = Path::find_data_file(filename);
  if (fullPath.empty()) {
    fprintf(stderr, "asset_load_bmp: Couldn't find %s in any search path!\n",
            filename);
    return nullptr;
  }

  SdlSurfacePtr_t surf(SDL_LoadBMP(fullPath.c_str()));
  if (surf != nullptr) {
    fprintf(stderr, "asset_load_bmp: Loaded %s from %s\n", filename,
            fullPath.c_str());
  }

  return surf;
}

auto asset_load_bmp(const char* filename) -> SDL_Surface* {
  return sdl_asset_load_bmp(filename).release();
}

static SdlSurfacePtr_t s_app_icon;

auto sdl_asset_free_icon() -> void {
  s_app_icon.reset();
  if (assets != nullptr) {
    assets->icon = nullptr;
  }
}

auto sdl_asset_load_icon() -> void {
  if (assets != nullptr) {
    sdl_asset_free_icon();
    asset_set_free_icon_callback(sdl_asset_free_icon);
    s_app_icon = sdl_asset_load_bmp("icon.bmp");
    assets->icon = static_cast<void*>(s_app_icon.get());
  }
}
