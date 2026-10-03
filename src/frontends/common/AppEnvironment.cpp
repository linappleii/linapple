// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/AppEnvironment.h"

#include <string>
#include <vector>

#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

constexpr const char* k_config_file_name = "linapple.conf";

auto app_env_resolve_paths(AppConfig_t* config) -> void {
  if (config == nullptr) {
    return;
  }

  const std::string user_config_dir = Path::get_user_config_dir();
  std::vector<std::string> search_paths;

  if (config->config_path.at(0) != '\0') {
    search_paths.emplace_back(config->config_path.data());
  }

  search_paths.emplace_back(Path::join(user_config_dir, k_config_file_name));
  search_paths.emplace_back(k_config_file_name);
  search_paths.emplace_back(Path::find_data_file(k_config_file_name));

  std::string final_path;
  for (const auto& path : search_paths) {
    if (path.empty()) {
      continue;
    }
    if (config->load(path)) {
      final_path = path;
      break;
    }
  }

  if (final_path.empty()) {
    Path::ensure_dir_exists(user_config_dir);
    final_path = Path::join(user_config_dir, k_config_file_name);
    config->set_path(final_path);
    util_safe_strcpy(config->config_path.data(), final_path.c_str(),
                     path_max_len);
  }

  if (config != &Configuration_t::instance()) {
    Configuration_t::instance() = *config;
  }

  Logger::initialize();

  if (config->is_verbose) {
    Logger::set_verbosity(LogLevel_t::perf);
  } else if (config->is_log) {
    Logger::set_verbosity(LogLevel_t::info);
  } else {
    Logger::set_verbosity(LogLevel_t::warning);
  }
}
