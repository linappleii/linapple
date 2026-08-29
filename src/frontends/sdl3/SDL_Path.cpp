#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <string>

#include "core/Util_Path.h"

namespace Path {

auto get_executable_dir() -> std::string {
  const char* base = SDL_GetBasePath();
  if (!base) {
    return "./";
  }
  return {base};
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
