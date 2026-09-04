// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl3/SDL_Asset.h"

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <cstdio>
#include <string>

#include "core/Asset.h"
#include "core/Util_Path.h"
#include "frontends/sdl3/SdlPtr.h"
#include "icon.xpm"

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
    // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-pro-type-reinterpret-cast)
    // Justification: SDL3_image IMG_ReadXPMFromArray requires non-const char**
    // parameter.
    s_icon_surface.reset(reinterpret_cast<SDL_Surface*>(
        IMG_ReadXPMFromArray(const_cast<char**>(icon_xpm))));
    // NOLINTEND(cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-pro-type-reinterpret-cast)
    assets->icon = s_icon_surface.get();
  }
}
