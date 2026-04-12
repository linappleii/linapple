#include "core/Common.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "core/asset.h"
#include "core/Util_Path.h"

#include "../../../res/icon.xpm"

SDL_Surface *Asset_LoadBMP(const char *filename)
{
  std::string fullPath = Path::FindDataFile(filename);
  if (fullPath.empty()) {
    fprintf(stderr, "Asset_LoadBMP: Couldn't find %s in any search path!\n", filename);
    return NULL;
  }

  SDL_Surface *surf = SDL_LoadBMP(fullPath.c_str());
  if (NULL != surf) {
    fprintf(stderr, "Asset_LoadBMP: Loaded %s from %s\n", filename, fullPath.c_str());
  }

  return surf;
}

void SDL_Asset_LoadIcon() {
    if (assets) {
        assets->icon = (void*)IMG_ReadXPMFromArray(icon_xpm);
    }
}

void SDL_Asset_FreeIcon() {
    if (assets && assets->icon) {
        SDL_DestroySurface((SDL_Surface*)assets->icon);
        assets->icon = nullptr;
    }
}
