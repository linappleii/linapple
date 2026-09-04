// SPDX-License-Identifier: GPL-2.0-only
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <cstdio>
#include <string>

#include "core/Asset.h"
#include "core/Util_Path.h"
#include "icon.xpm"

auto asset_load_bmp(const char* filename) -> SDL_Surface* {
  std::string fullPath = Path::find_data_file(filename);
  if (fullPath.empty()) {
    fprintf(stderr, "asset_load_bmp: Couldn't find %s in any search path!\n",
            filename);
    return nullptr;
  }

  SDL_Surface* surf = SDL_LoadBMP(fullPath.c_str());
  if (nullptr != surf) {
    fprintf(stderr, "asset_load_bmp: Loaded %s from %s\n", filename,
            fullPath.c_str());
  }

  return surf;
}

auto sdl_asset_free_icon() -> void {
  if (assets && assets->icon) {
    SDL_DestroySurface(static_cast<SDL_Surface*>(assets->icon));
    assets->icon = nullptr;
  }
}

auto sdl_asset_load_icon() -> void {
  if (assets) {
    sdl_asset_free_icon();
    asset_set_free_icon_callback(sdl_asset_free_icon);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-pro-type-reinterpret-cast)
    // Justification: SDL3_image IMG_ReadXPMFromArray requires non-const char** parameter.
    assets->icon = reinterpret_cast<void*>(
        IMG_ReadXPMFromArray(const_cast<char**>(icon_xpm)));
    // NOLINTEND(cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-pro-type-reinterpret-cast)
  }
}
