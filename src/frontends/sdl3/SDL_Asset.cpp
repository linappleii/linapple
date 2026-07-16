#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "core/Util_Path.h"
#include "core/Asset.h"
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

void SDL_Asset_LoadIcon() {
  if (assets) {
    assets->icon =
        reinterpret_cast<void*>(IMG_ReadXPMFromArray((char**)icon_xpm));
  }
}

void SDL_Asset_FreeIcon() {
  if (assets && assets->icon) {
    SDL_DestroySurface(static_cast<SDL_Surface*>(assets->icon));
    assets->icon = nullptr;
  }
}
