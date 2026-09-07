// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include <vector>

#include "core/config/ConfigSchema.h"

// Options controlling configuration search and discovery behavior.
struct ConfigSearchOptions_t {
  std::string cli_override;
  bool create_default_if_missing{true};
};

// Enumerates all candidate search paths across tiers in strict priority order:
// Tier 2 (CWD: ./linapple.toml, ./linapple.conf)
// Tier 3 (User: ~/.config/linapple/linapple.toml, .conf,
// ~/.linapple/linapple.conf) Tier 4 (Executable dir: <exe>/linapple.toml,
// <exe>/linapple.conf) Tier 5 (System: /etc/linapple/linapple.toml,
// /etc/linapple/linapple.conf, etc.)
auto config_get_search_paths() -> std::vector<std::string>;

// Enumerates candidate search paths for modern linapple.toml in deterministic
// priority order.
auto config_get_toml_search_paths() -> std::vector<std::string>;

// Enumerates candidate search paths for legacy linapple.conf in deterministic
// priority order.
auto config_get_legacy_search_paths() -> std::vector<std::string>;

// Discovers the highest priority existing configuration candidate:
// Priority 1: Explicit CLI override path (if specified in options.cli_override)
// Priority 2: Current working directory (./linapple.toml, fallback
// ./linapple.conf) Priority 3: User config directory
// (~/.config/linapple/linapple.toml, fallback .conf, ~/.linapple/linapple.conf)
// Priority 4: Executable directory (<exe>/linapple.toml, fallback
// <exe>/linapple.conf) Priority 5: System configuration directories
// (/etc/linapple/linapple.toml, fallback .conf) Returns true if an accessible
// configuration file was found.
auto config_discover(const ConfigSearchOptions_t& options,
                     std::string* out_resolved_path,
                     bool* out_is_legacy = nullptr) -> bool;

// Locates an existing linapple.toml file:
// If cli_override is non-empty, checks and returns it if it exists.
// Otherwise evaluates candidate search paths in order.
// Returns empty string if no TOML file is found.
auto config_locate_toml_file(const std::string& cli_override = "")
    -> std::string;

// Locates an existing legacy linapple.conf file across legacy search paths.
// Returns empty string if no legacy file is found.
auto config_locate_legacy_file() -> std::string;

// Returns the canonical default user TOML configuration path:
// ${XDG_CONFIG_HOME}/linapple/linapple.toml (or
// ~/.config/linapple/linapple.toml)
auto config_get_default_user_path() -> std::string;

// Creates a modern default configuration file at target_path:
// Ensures parent directories exist and serializes default LinAppleConfig_t to
// TOML. Returns true on success, or false with diagnostics in out_error.
auto config_create_default_file(const std::string& target_path,
                                std::string* out_error = nullptr) -> bool;

// Ensures a modern configuration file exists:
// If target_path is specified, checks/creates at target_path.
// Otherwise checks if any TOML file exists in search paths; if not, generates
// a documented default linapple.toml at config_get_default_user_path().
auto config_ensure_default_exists(const std::string& target_path = "",
                                  std::string* out_created_path = nullptr)
    -> bool;

// Comprehensive startup loader:
// 1. Defensively initializes *out_config to config_defaults().
// 2. Evaluates search tiers in strict priority order.
// 3. If a legacy configuration is selected, flags *out_is_legacy = true,
//    records path in *out_loaded_path, and returns true.
// 4. If a TOML file is selected, parses and validates it. If parsing/validation
//    fails, immediately returns false with diagnostic message in out_error.
// 5. If no file is found and options.create_default_if_missing is true,
//    generates default linapple.toml and loads it.
// 6. If no file is found and create_default_if_missing is false, returns false
// with out_error.
auto config_load_active(const ConfigSearchOptions_t& options,
                        LinAppleConfig_t* out_config,
                        std::string* out_loaded_path = nullptr,
                        bool* out_is_legacy = nullptr,
                        std::string* out_error = nullptr) -> bool;

// Convenience overload accepting a simple CLI override string.
auto config_load_active(const std::string& cli_override,
                        LinAppleConfig_t* out_config,
                        std::string* out_loaded_path = nullptr,
                        bool* out_is_legacy = nullptr,
                        std::string* out_error = nullptr) -> bool;
