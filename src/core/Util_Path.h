// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <linux/limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

// RAII wrapper for FILE*
using FilePtr_t = std::unique_ptr<FILE, int (*)(FILE*)>;

constexpr char file_separator = '/';
constexpr char ftp_separator = '/';

namespace Path {

constexpr mode_t k_default_mkdir_mode = 0755;

inline auto join(const std::string& dir, const std::string& filename)
    -> std::string {
  if (dir.empty()) {
    return filename;
  }
  if (filename.empty()) {
    return dir;
  }
  if (dir.back() == '/' && filename.front() == '/') {
    return dir + filename.substr(1);
  }
  if (dir.back() == '/' || filename.front() == '/') {
    return dir + filename;
  }
  return dir + "/" + filename;
}

inline auto ensure_dir_exists(const std::string& path) -> void {
  if (path.empty()) {
    return;
  }
  const std::string dir = (path.back() == '/') ? path : path + '/';
  size_t pos = dir.find_first_of('/');
  while (pos != std::string::npos) {
    std::string subdir = dir.substr(0, pos);
    pos = dir.find_first_of('/', pos + 1);
    if (subdir.empty() || subdir == "/") {
      continue;
    }
    struct stat st{};
    if (stat(subdir.c_str(), &st) != 0) {
      mkdir(subdir.c_str(), k_default_mkdir_mode);
    }
  }
}

inline auto get_executable_dir() -> std::string {
  std::array<char, PATH_MAX> buf{};
  ssize_t len = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (len != -1) {
    buf.at(static_cast<size_t>(len)) = '\0';
    std::string path(buf.data());
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos) {
      return path.substr(0, pos + 1);
    }
  }
  return "./";
}

inline auto get_user_data_dir() -> std::string {
  const char* data_home = getenv("XDG_DATA_HOME");
  if (data_home != nullptr && data_home[0] != '\0') {
    return join(data_home, "linapple/");
  }
  const char* home = getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return join(home, ".local/share/linapple/");
  }
  return "./";
}

inline auto get_user_config_dir() -> std::string {
  const char* config_home = getenv("XDG_CONFIG_HOME");
  if (config_home != nullptr && config_home[0] != '\0') {
    return join(config_home, "linapple/");
  }
  const char* home = getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return join(home, ".config/linapple/");
  }
  return get_user_data_dir();
}

inline auto get_plugin_search_paths() -> std::vector<std::string> {
  std::vector<std::string> paths;

  paths.emplace_back(join(get_user_data_dir(), "plugins/"));
  const std::string exec_dir = get_executable_dir();
  paths.emplace_back(exec_dir);
  paths.emplace_back(join(exec_dir, "plugins/"));

  paths.emplace_back("/usr/local/lib/linapple/plugins/");
  paths.emplace_back("/usr/lib/linapple/plugins/");

  return paths;
}

inline auto get_data_search_paths() -> std::vector<std::string> {
  std::vector<std::string> paths;

  paths.emplace_back(get_user_data_dir());
  paths.emplace_back(get_user_config_dir());

  const std::string exec_dir = get_executable_dir();
  for (const char* subpath : {
           "",
           "res/",
           "../res/",
           "../../res/",
           "../../../res/",
           "../share/linapple/",
           "../../share/linapple/",
           "../etc/linapple/",
           "../../etc/linapple/",
       }) {
    paths.emplace_back(join(exec_dir, subpath));
  }

  paths.emplace_back("/usr/local/share/linapple/");
  paths.emplace_back("/usr/share/linapple/");

  const char* config_dirs = getenv("XDG_CONFIG_DIRS");
  if (config_dirs == nullptr || config_dirs[0] == '\0') {
    paths.emplace_back("/etc/xdg/linapple/");
  } else {
    std::string cd(config_dirs);
    size_t start = 0;
    while (start < cd.length()) {
      const size_t end = cd.find(':', start);
      const std::string dir = cd.substr(
          start, (end == std::string::npos) ? std::string::npos : end - start);
      if (!dir.empty()) {
        paths.emplace_back(join(dir, "linapple/"));
      }
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
  }

  paths.emplace_back("/etc/linapple/");
  return paths;
}

inline auto find_data_file(const std::string& filename) -> std::string {
  if (filename.empty()) {
    return "";
  }
  for (const auto& dir : get_data_search_paths()) {
    std::string full_path = join(dir, filename);
    if (access(full_path.c_str(), R_OK) == 0) {
      return full_path;
    }
  }
  return "";
}

inline auto sanitize_filename(const std::string& name) -> std::string {
  if (name.empty()) {
    return "";
  }
  if (name.find('/') != std::string::npos ||
      name.find('\\') != std::string::npos) {
    return "";
  }
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

// Centralizes bounds-checked file stream size validation and guarantees
// that original stream seek position is preserved across querying.
inline auto file_size(FILE* file) -> int64_t {
  if (file == nullptr) {
    return -1;
  }
  const long original_pos = ftell(file);
  if (original_pos < 0) {
    return -1;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fseek(file, original_pos, SEEK_SET);
    return -1;
  }
  const long end_pos = ftell(file);
  if (fseek(file, original_pos, SEEK_SET) != 0 || end_pos < 0) {
    return -1;
  }
  return static_cast<int64_t>(end_pos);
}

}  // namespace Path
