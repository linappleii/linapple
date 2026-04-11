#include "core/Util_Path.h"
#include <SDL3/SDL.h>

namespace Path {

std::string GetExecutableDir() {
    const char* base = SDL_GetBasePath();
    if (!base) return "./";
    return std::string(base);
}

std::string GetUserDataDir() {
    const char* pref = SDL_GetPrefPath(NULL, "linapple");
    if (!pref) return "./";
    return std::string(pref);
}

} // namespace Path
