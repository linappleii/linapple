#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "core/Util_Path.h"
#include "core/Asset.h"
#include "icon.xpm"

auto Asset_LoadBMP(const char* filename) -> SDL_Surface* {
  std::string fullPath = Path::FindDataFile(filename);
  if (fullPath.empty()) {
    fprintf(stderr, "Asset_LoadBMP: Couldn't find %s in any search path!\n",
            filename);
    return nullptr;
  }

  SDL_Surface* surf = SDL_LoadBMP(fullPath.c_str());
  if (nullptr != surf) {
    fprintf(stderr, "Asset_LoadBMP: Loaded %s from %s\n", filename,
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
