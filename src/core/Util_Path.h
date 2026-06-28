// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

// RAII wrapper for FILE*
using FilePtr = std::unique_ptr<FILE, int (*)(FILE*)>;

constexpr char FILE_SEPARATOR = '/';
constexpr char FTP_SEPARATOR = '/';

namespace Path {

// Ensure directory exists (creates it recursively if it doesn't)
inline void EnsureDirExists(const std::string& path) {
  size_t pos = 0;
  do {
    pos = path.find_first_of('/', pos + 1);
    std::string subdir = path.substr(0, pos);
    if (!subdir.empty() && subdir != "/") {
      struct stat st;
      if (stat(subdir.c_str(), &st) != 0) {
        mkdir(subdir.c_str(), 0755);
      }
    }
  } while (pos != std::string::npos);
}

// Returns the directory where the executable is located.
auto GetExecutableDir() -> std::string;

// Returns the user's data directory (~/.local/share/linapple/)
auto GetUserDataDir() -> std::string;

// Returns the user's configuration directory (~/.config/linapple/)
inline auto GetUserConfigDir() -> std::string {
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

// Returns a list of directories to search for shared object plugins (.so files)
inline auto GetPluginSearchPaths() -> std::vector<std::string> {
  std::vector<std::string> paths;

  paths.push_back(GetUserDataDir() + "plugins/");

  const char* dataHome = getenv("XDG_DATA_HOME");
  if (dataHome) {
    paths.push_back(std::string(dataHome) + "/linapple/plugins/");
  } else {
    const char* home = getenv("HOME");
    if (home) {
      paths.push_back(std::string(home) + "/.local/share/linapple/plugins/");
    }
  }

  paths.push_back(GetExecutableDir());
  paths.push_back(GetExecutableDir() + "plugins/");

  paths.push_back("/usr/local/lib/linapple/plugins/");
  paths.push_back("/usr/lib/linapple/plugins/");

  return paths;
}

// Returns a list of directories to search for data assets (ROMs, disks,
// config).
inline auto GetDataSearchPaths() -> std::vector<std::string> {
  std::vector<std::string> paths;

  paths.push_back(GetUserDataDir());
  paths.push_back(GetUserConfigDir());
  paths.push_back(GetExecutableDir());
  paths.push_back(GetExecutableDir() + "res/");
  paths.push_back(GetExecutableDir() + "../res/");

  paths.push_back(GetExecutableDir() + "../share/linapple/");
  paths.push_back(GetExecutableDir() + "../etc/linapple/");

#ifdef ASSET_DIR
  paths.push_back(ASSET_DIR "/");
#endif
#ifdef SYSCONF_DIR
  paths.push_back(SYSCONF_DIR "/");
#endif

  return paths;
}

inline auto Join(const std::string& dir, const std::string& filename)
    -> std::string {
  if (dir.empty()) {
    return filename;
  }
  if (filename.empty()) {
    return dir;
  }
  if (dir.back() == '/') {
    return dir + filename;
  }
  return dir + "/" + filename;
}

inline auto FindDataFile(const std::string& filename) -> std::string {
  for (const auto& dir : GetDataSearchPaths()) {
    std::string fullPath = Join(dir, filename);
    if (access(fullPath.c_str(), R_OK) == 0) {
      return fullPath;
    }
  }
  return "";
}

inline auto CopyFile(const std::string& src, const std::string& dst) -> bool {
  std::ifstream src_file(src, std::ios::binary);
  if (!src_file.is_open()) return false;
  std::ofstream dst_file(dst, std::ios::binary);
  if (!dst_file.is_open()) return false;
  dst_file << src_file.rdbuf();
  return true;
}

}  // namespace Path
