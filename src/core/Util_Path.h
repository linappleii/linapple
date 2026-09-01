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
using FilePtr_t = std::unique_ptr<FILE, int (*)(FILE*)>;

constexpr char file_separator = '/';
constexpr char ftp_separator = '/';

namespace Path {

constexpr mode_t DEFAULT_MKDIR_MODE = 0755;

// Ensure directory exists (creates it recursively if it doesn't)
inline void EnsureDirExists(const std::string& path) {
  size_t pos = path.find_first_of('/');
  while (pos != std::string::npos) {
    std::string subdir = path.substr(0, pos);
    if (!subdir.empty() && subdir != "/") {
      struct stat st{};
      if (stat(subdir.c_str(), &st) != 0) {
        mkdir(subdir.c_str(), DEFAULT_MKDIR_MODE);
      }
    }
    pos = path.find_first_of('/', pos + 1);
  }
}

// Returns the directory where the executable is located.
auto get_executable_dir() -> std::string;

// Returns user's linapple data directory (~/.local/share/linapple/).
auto get_user_data_dir() -> std::string;

// Returns user's linapple config directory (~/.config/linapple/).
inline auto get_user_config_dir() -> std::string {
  const char* configHome = getenv("XDG_CONFIG_HOME");
  if (configHome) {
    return std::string(configHome) + "/linapple/";
  }
  const char* home = getenv("HOME");
  if (home) {
    return std::string(home) + "/.config/linapple/";
  }
  return get_user_data_dir();
}

// Returns a list of directories to search for peripheral plugins (.so files).
inline auto get_plugin_search_paths() -> std::vector<std::string> {
  std::vector<std::string> paths;

  paths.emplace_back(get_user_data_dir() + "plugins/");

  const char* dataHome = getenv("XDG_DATA_HOME");
  if (dataHome) {
    paths.emplace_back(std::string(dataHome) + "/linapple/plugins/");
  } else {
    const char* home = getenv("HOME");
    if (home) {
      paths.emplace_back(std::string(home) + "/.local/share/linapple/plugins/");
    }
  }

  paths.emplace_back(get_executable_dir());
  paths.emplace_back(get_executable_dir() + "plugins/");

  paths.emplace_back("/usr/local/lib/linapple/plugins/");
  paths.emplace_back("/usr/lib/linapple/plugins/");

  return paths;
}

inline auto join(const std::string& dir, const std::string& filename)
    -> std::string;

// Returns a list of directories to search for data assets (ROMs, disks,
// config).
inline auto get_data_search_paths() -> std::vector<std::string> {
  std::vector<std::string> paths;

  paths.emplace_back(get_user_data_dir());
  paths.emplace_back(get_user_config_dir());
  paths.emplace_back(get_executable_dir());
  paths.emplace_back(get_executable_dir() + "res/");
  paths.emplace_back(get_executable_dir() + "../res/");
  paths.emplace_back(get_executable_dir() + "../../res/");
  paths.emplace_back(get_executable_dir() + "../../../res/");

  paths.emplace_back(get_executable_dir() + "../share/linapple/");
  paths.emplace_back(get_executable_dir() + "../../share/linapple/");
  paths.emplace_back(get_executable_dir() + "../etc/linapple/");
  paths.emplace_back(get_executable_dir() + "../../etc/linapple/");

#ifdef SOURCE_RES_DIR
  paths.emplace_back(SOURCE_RES_DIR "/");
#endif
#ifdef ASSET_DIR
  paths.emplace_back(ASSET_DIR "/");
#endif
#ifdef SYSCONF_DIR
  paths.emplace_back(SYSCONF_DIR "/");
#endif
#ifdef SYS_DIR
  paths.emplace_back(SYS_DIR "/");
#endif

  const char* configDirs = getenv("XDG_CONFIG_DIRS");
  if (configDirs != nullptr) {
    std::string cd(configDirs);
    size_t start = 0;
    while (start < cd.length()) {
      size_t end = cd.find(':', start);
      if (end == std::string::npos) end = cd.length();
      std::string dir = cd.substr(start, end - start);
      if (!dir.empty()) {
        paths.emplace_back(join(dir, "linapple/"));
      }
      start = end + 1;
    }
  } else {
    paths.emplace_back("/etc/xdg/linapple/");
  }
  paths.emplace_back("/etc/linapple/");

  return paths;
}

inline auto join(const std::string& dir, const std::string& filename)
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

inline auto find_data_file(const std::string& filename) -> std::string {
  for (const auto& dir : get_data_search_paths()) {
    std::string fullPath = join(dir, filename);
    if (access(fullPath.c_str(), R_OK) == 0) {
      return fullPath;
    }
  }
  return "";
}

inline auto copy_file(const std::string& src, const std::string& dst) -> bool {
  std::ifstream src_file(src, std::ios::binary);
  if (!src_file.is_open()) return false;
  std::ofstream dst_file(dst, std::ios::binary);
  if (!dst_file.is_open()) return false;
  dst_file << src_file.rdbuf();
  return true;
}

inline auto sanitize_filename(const std::string& name) -> std::string {
  if (name.empty()) {
    return "";
  }
  // Strictly reject any filename containing path separators or directory
  // components
  if (name.find('/') != std::string::npos ||
      name.find('\\') != std::string::npos) {
    return "";
  }
  // Reject relative navigation and hidden dotfiles
  if (name == "." || name == ".." || name.front() == '.') {
    return "";
  }
  constexpr unsigned char ascii_del = 0x7F;
  for (char c : name) {
    auto uc = static_cast<unsigned char>(c);
    if (std::iscntrl(uc) || uc == ascii_del) {
      return "";
    }
  }
  return name;
}

}  // namespace Path
