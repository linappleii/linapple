#include <linux/limits.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <string>

#include "core/Util_Path.h"

namespace Path {

auto get_executable_dir() -> std::string {
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

auto get_user_data_dir() -> std::string {
  const char* dataHome = getenv("XDG_DATA_HOME");
  if (dataHome) {
    return std::string(dataHome) + "/linapple/";
  }
  const char* home = getenv("HOME");
  if (home) {
    return std::string(home) + "/.local/share/linapple/";
  }
  return "./";
}

}  // namespace Path