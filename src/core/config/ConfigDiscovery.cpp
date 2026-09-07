// SPDX-License-Identifier: GPL-2.0-only
#include "core/config/ConfigDiscovery.h"

#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/Util_Path.h"
#include "core/config/ConfigSchema.h"
#include "core/config/Toml.h"

namespace {

auto add_unique_path(std::vector<std::string>& paths, const std::string& path)
    -> void {
  if (path.empty()) {
    return;
  }
  for (const auto& p : paths) {
    if (p == path) {
      return;
    }
  }
  paths.push_back(path);
}

auto is_legacy_config_file(const std::string& path) -> bool {
  if (path.size() < 5) {
    return false;
  }
  std::string ext = path.substr(path.size() - 5);
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext == ".conf";
}

}  // namespace

auto config_get_search_paths() -> std::vector<std::string> {
  std::vector<std::string> paths;

  // Tier 2: Current working directory
  add_unique_path(paths, "./linapple.toml");
  add_unique_path(paths, "./linapple.conf");

  // Tier 3: User configuration directory
  add_unique_path(paths, Path::get_user_config_dir() + "linapple.toml");
  add_unique_path(paths, Path::get_user_config_dir() + "linapple.conf");
  const char* home = getenv("HOME");
  if (home != nullptr) {
    add_unique_path(paths, std::string(home) + "/.linapple/linapple.conf");
  }

  // Tier 4: Executable directory
  const std::string exe_dir = Path::get_executable_dir();
  if (!exe_dir.empty()) {
    add_unique_path(paths, Path::join(exe_dir, "linapple.toml"));
    add_unique_path(paths, Path::join(exe_dir, "linapple.conf"));
  }

  // Tier 5: System configuration directories
  const char* config_dirs = getenv("XDG_CONFIG_DIRS");
  if (config_dirs != nullptr) {
    std::string cd(config_dirs);
    size_t start = 0;
    while (start < cd.length()) {
      size_t end = cd.find(':', start);
      if (end == std::string::npos) {
        end = cd.length();
      }
      std::string dir = cd.substr(start, end - start);
      if (!dir.empty()) {
        add_unique_path(paths, Path::join(dir, "linapple/linapple.toml"));
        add_unique_path(paths, Path::join(dir, "linapple/linapple.conf"));
      }
      start = end + 1;
    }
  } else {
    add_unique_path(paths, "/etc/xdg/linapple/linapple.toml");
    add_unique_path(paths, "/etc/xdg/linapple/linapple.conf");
  }

  add_unique_path(paths, "/etc/linapple/linapple.toml");
  add_unique_path(paths, "/etc/linapple/linapple.conf");

  return paths;
}

auto config_get_toml_search_paths() -> std::vector<std::string> {
  std::vector<std::string> paths;
  for (const auto& path : config_get_search_paths()) {
    if (!is_legacy_config_file(path)) {
      paths.push_back(path);
    }
  }
  return paths;
}

auto config_get_legacy_search_paths() -> std::vector<std::string> {
  std::vector<std::string> paths;
  for (const auto& path : config_get_search_paths()) {
    if (is_legacy_config_file(path)) {
      paths.push_back(path);
    }
  }
  return paths;
}

auto config_discover(const ConfigSearchOptions_t& options,
                     std::string* out_resolved_path, bool* out_is_legacy)
    -> bool {
  if (!options.cli_override.empty()) {
    if (access(options.cli_override.c_str(), R_OK) == 0) {
      if (out_resolved_path != nullptr) {
        *out_resolved_path = options.cli_override;
      }
      if (out_is_legacy != nullptr) {
        *out_is_legacy = is_legacy_config_file(options.cli_override);
      }
      return true;
    }
    return false;
  }

  for (const auto& path : config_get_search_paths()) {
    if (access(path.c_str(), R_OK) == 0) {
      if (out_resolved_path != nullptr) {
        *out_resolved_path = path;
      }
      if (out_is_legacy != nullptr) {
        *out_is_legacy = is_legacy_config_file(path);
      }
      return true;
    }
  }

  return false;
}

auto config_locate_toml_file(const std::string& cli_override) -> std::string {
  if (!cli_override.empty()) {
    if (access(cli_override.c_str(), R_OK) == 0 &&
        !is_legacy_config_file(cli_override)) {
      return cli_override;
    }
    return "";
  }

  for (const auto& path : config_get_toml_search_paths()) {
    if (access(path.c_str(), R_OK) == 0) {
      return path;
    }
  }

  return "";
}

auto config_locate_legacy_file() -> std::string {
  for (const auto& path : config_get_legacy_search_paths()) {
    if (access(path.c_str(), R_OK) == 0) {
      return path;
    }
  }
  return "";
}

auto config_get_default_user_path() -> std::string {
  return Path::get_user_config_dir() + "linapple.toml";
}

auto config_create_default_file(const std::string& target_path,
                                std::string* out_error) -> bool {
  const std::string path =
      target_path.empty() ? config_get_default_user_path() : target_path;

  if (access(path.c_str(), F_OK) == 0) {
    return true;
  }

  Path::ensure_dir_exists(path);

  const LinAppleConfig_t default_config;
  auto default_doc = config_to_toml(default_config);
  if (default_doc == nullptr) {
    if (out_error != nullptr) {
      *out_error = "Failed to serialize default configuration to TOML";
    }
    return false;
  }

  std::string save_err;
  if (!toml_document_save_file(default_doc.get(), path, &save_err)) {
    if (out_error != nullptr) {
      *out_error =
          "Failed to save default TOML file to '" + path + "': " + save_err;
    }
    return false;
  }

  return true;
}

auto config_ensure_default_exists(const std::string& target_path,
                                  std::string* out_created_path) -> bool {
  const std::string path =
      target_path.empty() ? config_get_default_user_path() : target_path;

  std::string err;
  if (!config_create_default_file(path, &err)) {
    return false;
  }

  if (out_created_path != nullptr) {
    *out_created_path = path;
  }
  return true;
}

auto config_load_active(const ConfigSearchOptions_t& options,
                        LinAppleConfig_t* out_config,
                        std::string* out_loaded_path, bool* out_is_legacy,
                        std::string* out_error) -> bool {
  if (out_config == nullptr) {
    if (out_error != nullptr) {
      *out_error = "out_config pointer is null";
    }
    return false;
  }

  // Defensively initialize out_config to defaults
  *out_config = config_defaults();

  if (out_is_legacy != nullptr) {
    *out_is_legacy = false;
  }
  if (out_loaded_path != nullptr) {
    out_loaded_path->clear();
  }

  // 1. Explicit CLI override
  if (!options.cli_override.empty()) {
    if (access(options.cli_override.c_str(), R_OK) != 0) {
      if (out_error != nullptr) {
        *out_error =
            "Specified configuration file not found or inaccessible: " +
            options.cli_override;
      }
      return false;
    }

    if (is_legacy_config_file(options.cli_override)) {
      if (out_is_legacy != nullptr) {
        *out_is_legacy = true;
      }
      if (out_loaded_path != nullptr) {
        *out_loaded_path = options.cli_override;
      }
      return true;
    }

    std::string parse_err;
    auto doc = toml_document_load_file(options.cli_override, &parse_err);
    if (doc == nullptr) {
      if (out_error != nullptr) {
        *out_error = "Failed to parse TOML configuration from '" +
                     options.cli_override + "': " + parse_err;
      }
      return false;
    }

    std::vector<ConfigValidationError_t> val_errs;
    if (!config_from_toml(doc.get(), out_config, &val_errs)) {
      if (out_error != nullptr) {
        *out_error = "Configuration schema validation failed for '" +
                     options.cli_override + "'";
        if (!val_errs.empty()) {
          *out_error += ": [" + val_errs[0].section + "]." + val_errs[0].key +
                        ": " + val_errs[0].message;
        }
      }
      return false;
    }

    if (out_loaded_path != nullptr) {
      *out_loaded_path = options.cli_override;
    }
    return true;
  }

  // 2. Discover existing configuration across tiered search paths
  std::string discovered_path;
  bool is_legacy = false;
  if (config_discover(options, &discovered_path, &is_legacy)) {
    if (is_legacy) {
      if (out_is_legacy != nullptr) {
        *out_is_legacy = true;
      }
      if (out_loaded_path != nullptr) {
        *out_loaded_path = discovered_path;
      }
      return true;
    }

    // Modern TOML candidate selected: must parse and validate successfully
    std::string parse_err;
    auto doc = toml_document_load_file(discovered_path, &parse_err);
    if (doc == nullptr) {
      if (out_error != nullptr) {
        *out_error = "Failed to parse TOML configuration from '" +
                     discovered_path + "': " + parse_err;
      }
      return false;
    }

    std::vector<ConfigValidationError_t> val_errs;
    if (!config_from_toml(doc.get(), out_config, &val_errs)) {
      if (out_error != nullptr) {
        *out_error = "Configuration schema validation failed for '" +
                     discovered_path + "'";
        if (!val_errs.empty()) {
          *out_error += ": [" + val_errs[0].section + "]." + val_errs[0].key +
                        ": " + val_errs[0].message;
        }
      }
      return false;
    }

    if (out_loaded_path != nullptr) {
      *out_loaded_path = discovered_path;
    }
    return true;
  }

  // 3. No existing file found: check create_default_if_missing
  if (!options.create_default_if_missing) {
    if (out_error != nullptr) {
      *out_error =
          "No configuration file found in search paths and default creation is "
          "disabled";
    }
    return false;
  }

  // 4. Auto-generate modern default linapple.toml
  const std::string default_path = config_get_default_user_path();
  std::string create_err;
  if (!config_create_default_file(default_path, &create_err)) {
    if (out_error != nullptr) {
      *out_error = "Failed to create default configuration file at '" +
                   default_path + "': " + create_err;
    }
    return false;
  }

  std::string parse_err;
  auto doc = toml_document_load_file(default_path, &parse_err);
  if (doc == nullptr) {
    if (out_error != nullptr) {
      *out_error =
          "Failed to parse newly created default configuration: " + parse_err;
    }
    return false;
  }

  std::vector<ConfigValidationError_t> val_errs;
  if (!config_from_toml(doc.get(), out_config, &val_errs)) {
    if (out_error != nullptr) {
      *out_error =
          "Newly created default configuration failed schema validation";
    }
    return false;
  }

  if (out_loaded_path != nullptr) {
    *out_loaded_path = default_path;
  }
  return true;
}

auto config_load_active(const std::string& cli_override,
                        LinAppleConfig_t* out_config,
                        std::string* out_loaded_path, bool* out_is_legacy,
                        std::string* out_error) -> bool {
  ConfigSearchOptions_t options;
  options.cli_override = cli_override;
  options.create_default_if_missing = true;
  return config_load_active(options, out_config, out_loaded_path, out_is_legacy,
                            out_error);
}
