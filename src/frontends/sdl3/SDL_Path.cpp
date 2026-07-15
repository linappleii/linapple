#include <SDL3/SDL.h>

#include "core/Util_Path.h"

namespace Path {

auto get_executable_dir() -> std::string {
  const char* base = SDL_GetBasePath();
  if (!base) {
    return "./";
  }
  return std::string(base);
}

auto get_user_data_dir() -> std::string {
  const char* pref = SDL_GetPrefPath(nullptr, "linapple");
  if (!pref) {
    return "./";
  }
  std::string path(pref);
  SDL_free(const_cast<char*>(pref));
  return path;
}

}  // namespace Path
