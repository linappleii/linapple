// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <memory>
#include <string>

#include "core/config/ConfigSchema.h"
#include "core/config/Toml.h"

// Migrates an in-memory legacy linapple.conf INI string into a modern
// LinAppleConfig_t structure.
// Returns true on success, or false with diagnostics in out_error.
auto config_migrate_legacy_ini(const std::string& ini_content,
                               LinAppleConfig_t* out_config,
                               std::string* out_error = nullptr) -> bool;

// Migrates legacy INI content directly into a modern TomlDocument_t,
// converting numeric codes to human-readable strings, regrouping peripheral
// settings into [Peripheral.<Name>] sections, and preserving custom sections
// like [Keyboard.Custom] and [Custom ROM].
// Returns pointer to TomlDocument_t on success, or nullptr with diagnostics in
// out_error.
auto config_migrate_legacy_to_toml(const std::string& ini_content,
                                   std::string* out_error = nullptr)
    -> std::unique_ptr<TomlDocument_t>;

// Upgrades a legacy linapple.conf file on disk to a modern linapple.toml file:
// 1. Reads legacy_conf_path.
// 2. Converts all sections and keys to modern TOML schema with human-readable
// values.
// 3. Writes modern TOML to target_toml_path (ensuring parent directories
// exist). If target_toml_path is empty, defaults to
// config_get_default_user_path(). Returns true on success, or false with
// diagnostics in out_error.
auto config_upgrade_legacy(const std::string& legacy_conf_path,
                           const std::string& target_toml_path = "",
                           std::string* out_error = nullptr) -> bool;
