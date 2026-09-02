#include "frontends/common/AppEnvironment.h"

#include <string>
#include <vector>

#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/AppConfig.h"

static constexpr const char* CONFIG_FILE_NAME = "linapple.conf";

void app_env_resolve_paths(AppConfig_t* config) {
  if (config == nullptr) {
    return;
  }

  std::vector<std::string> searchPaths;

  // 1. Explicit --config CLI override
  if (config->config_path.at(0) != '\0') {
    searchPaths.emplace_back(config->config_path.data());
  }

  // 2. XDG Base Directory Specification (~/.config/linapple/)
  searchPaths.emplace_back(Path::get_user_config_dir() + CONFIG_FILE_NAME);

  // 3. Current Working Directory
  searchPaths.emplace_back(CONFIG_FILE_NAME);

  // 4. System-wide installation paths
  // FindDataFile handles /etc/linapple/ and /usr/share/linapple/ via
  // GetDataSearchPaths
  searchPaths.emplace_back(Path::find_data_file(CONFIG_FILE_NAME));

  std::string finalPath;
  bool loaded = false;

  for (const auto& path : searchPaths) {
    if (path.empty()) continue;
    if (Configuration_t::instance().load(path)) {
      finalPath = path;
      loaded = true;
      break;
    }
  }

  // Fallback: if nothing loaded, use XDG path even if it doesn't exist yet
  if (!loaded) {
    finalPath = Path::get_user_config_dir() + CONFIG_FILE_NAME;
    Path::ensure_dir_exists(Path::get_user_config_dir());
    // We don't call Load() again here as we know it's not there or failed,
    // we just want to set the path where it *should* be saved later.
  }

  // Populate back to config
  util_safe_strcpy(config->config_path.data(), finalPath.c_str(), path_max_len);

  // Consolidate Logger initialization
  Logger::initialize();

  // Set verbosity based on config
  if (config->is_verbose) {
    Logger::set_verbosity(LogLevel_t::k_perf);
  } else if (config->is_log) {
    Logger::set_verbosity(LogLevel_t::k_info);
  } else {
    // Default to errors and warnings only to keep console clean for normal use
    Logger::set_verbosity(LogLevel_t::k_warning);
  }
}
