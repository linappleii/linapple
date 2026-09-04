// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl2/SDL_Asset.h"

#include <SDL_surface.h>

#include <cstdio>
#include <string>

#include "core/Asset.h"
#include "core/Util_Path.h"
#include "frontends/sdl2/SdlPtr.h"

static SdlSurfacePtr_t s_icon_surface;

auto asset_load_bmp(const char* filename) -> SdlSurfacePtr_t {
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

auto sdl_asset_free_icon() -> void {
  s_icon_surface.reset();
  if (assets != nullptr) {
    assets->icon = nullptr;
  }
}

auto sdl_asset_load_icon() -> void {
  if (assets != nullptr) {
    sdl_asset_free_icon();
    asset_set_free_icon_callback(sdl_asset_free_icon);
    s_icon_surface = asset_load_bmp("icon.bmp");
    assets->icon = s_icon_surface.get();
  }
}
