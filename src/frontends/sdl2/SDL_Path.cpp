// SPDX-License-Identifier: GPL-2.0-only
#include <SDL_filesystem.h>

#include <string>

#include "core/Util_Path.h"

namespace Path {

auto get_executable_dir() -> std::string {
  const char* base = SDL_GetBasePath();
  if (base == nullptr) {
    return {"./"};
  }
  return {base};
}

auto get_user_data_dir() -> std::string {
  const char* pref = SDL_GetPrefPath(nullptr, "linapple");
  if (pref == nullptr) {
    return {"./"};
  }
  return {pref};
}

}  // namespace Path
