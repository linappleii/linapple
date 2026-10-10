// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl3/SDL_Asset.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_surface.h>

#include <cstdio>
#include <string>

#include "core/Asset.h"
#include "core/Util_Path.h"
#include "frontends/sdl3/SdlPtr.h"

namespace {
SdlSurfacePtr icon_surface;
}  // namespace

auto sdl_asset_load_bmp(const char* filename) -> SdlSurfacePtr {
  if (filename == nullptr) {
    return nullptr;
  }

  const std::string full_path = Path::find_data_file(filename);
  if (full_path.empty()) {
    std::fprintf(stderr,
                 "sdl_asset_load_bmp: Couldn't find %s in any search path!\n",
                 filename);
    return nullptr;
  }

  SdlSurfacePtr surf(SDL_LoadBMP(full_path.c_str()));
  if (surf == nullptr) {
    std::fprintf(stderr, "sdl_asset_load_bmp: Failed to load %s from %s: %s\n",
                 filename, full_path.c_str(), SDL_GetError());
    return nullptr;
  }

  std::fprintf(stderr, "sdl_asset_load_bmp: Loaded %s from %s\n", filename,
               full_path.c_str());
  return surf;
}

auto asset_load_bmp(const char* filename) -> SdlSurfacePtr {
  return sdl_asset_load_bmp(filename);
}

auto sdl_asset_free_icon() -> void {
  icon_surface.reset();
  if (assets == nullptr) {
    return;
  }
  assets->icon = nullptr;
}

auto sdl_asset_load_icon() -> void {
  if (assets == nullptr) {
    return;
  }

  sdl_asset_free_icon();
  asset_set_free_icon_callback(sdl_asset_free_icon);
  icon_surface = sdl_asset_load_bmp("icon.bmp");
  if (icon_surface == nullptr) {
    std::fprintf(stderr, "sdl_asset_load_icon: Failed to load icon: %s\n",
                 SDL_GetError());
    return;
  }
  assets->icon = static_cast<void*>(icon_surface.get());
}
