#include <SDL2/SDL.h>

#include "core/Util_Path.h"
#include "core/Asset.h"

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
  if (assets != nullptr) {
    assets->icon = static_cast<void*>(Asset_LoadBMP("icon.bmp"));
  }
}

void SDL_Asset_FreeIcon() {
  if (assets != nullptr && assets->icon != nullptr) {
    SDL_FreeSurface(static_cast<SDL_Surface*>(assets->icon));
    assets->icon = nullptr;
  }
}
