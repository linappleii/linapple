#include <SDL2/SDL.h>

#include "core/Util_Path.h"

namespace Path {

auto GetExecutableDir() -> std::string {
  const char* base = SDL_GetBasePath();
  if (base == nullptr) {
    return {"./"};
  }
  return {base};
}

auto GetUserDataDir() -> std::string {
  const char* pref = SDL_GetPrefPath(nullptr, "linapple");
  if (pref == nullptr) {
    return {"./"};
  }
  return {pref};
}

}  // namespace Path
