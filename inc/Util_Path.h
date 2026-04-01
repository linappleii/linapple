#ifndef UTIL_PATH_H
#define UTIL_PATH_H

#include <string>
#include <vector>
#include <unistd.h>
#include <fstream>
#include "shim.h"

namespace Path {

// Returns the directory where the executable is located.
inline std::string GetExecutableDir() {
    char* base = SDL_GetBasePath();
    if (!base) return "./";
    std::string result(base);
    SDL_free(base);
    return result;
}

// Returns the user's data directory (~/.local/share/linapple/)
inline std::string GetUserDataDir() {
    char* pref = SDL_GetPrefPath(NULL, "linapple");
    if (!pref) return "./";
    std::string result(pref);
    SDL_free(pref);
    return result;
}

// Returns the user's configuration directory (~/.config/linapple/)
inline std::string GetUserConfigDir() {
    const char* configHome = getenv("XDG_CONFIG_HOME");
    if (configHome) {
        return std::string(configHome) + "/linapple/";
    }
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/linapple/";
    }
    return GetUserDataDir();
}

// Returns a list of directories to search for data assets (ROMs, disks).
inline std::vector<std::string> GetDataSearchPaths() {
    std::vector<std::string> paths;

    paths.push_back(GetUserDataDir());
    paths.push_back(GetUserConfigDir());
    paths.push_back(GetExecutableDir());

#ifdef ASSET_DIR
    paths.push_back(ASSET_DIR "/");
#endif
    paths.push_back("/usr/local/share/linapple/");
    paths.push_back("/usr/share/linapple/");

    return paths;
}

// Attempts to find a file in the data search paths.
inline std::string FindDataFile(const std::string& filename) {
    for (const auto& dir : GetDataSearchPaths()) {
        std::string fullPath = dir + filename;
        if (access(fullPath.c_str(), R_OK) == 0) {
            return fullPath;
        }
    }
    return "";
}

// Standard binary file copy
inline bool CopyFile(const std::string& src, const std::string& dst) {
    std::ifstream src_file(src, std::ios::binary);
    if (!src_file.is_open()) return false;
    std::ofstream dst_file(dst, std::ios::binary);
    if (!dst_file.is_open()) return false;
    dst_file << src_file.rdbuf();
    return true;
}

} // namespace Path

#endif
