#include <SDL2/SDL.h>

#include "core/Asset.h"
#include "core/Util_Path.h"

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
  if (assets != nullptr) {
    assets->icon = static_cast<void*>(asset_load_bmp("icon.bmp"));
  }
}

void SDL_Asset_FreeIcon() {
  if (assets != nullptr && assets->icon != nullptr) {
    SDL_FreeSurface(static_cast<SDL_Surface*>(assets->icon));
    assets->icon = nullptr;
  }
}
