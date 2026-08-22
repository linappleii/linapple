// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <string>

enum BasicLineMode_t {
  basic_line_mode_explicit = 0,
  basic_line_mode_positional = 1
};

struct BasicSyncConfig_t {
  std::string file_path;
  BasicLineMode_t line_mode = basic_line_mode_explicit;
  bool enabled = false;
};

auto basic_sync_init(const char* file_path, BasicLineMode_t mode) -> void;
auto basic_sync_shutdown() -> void;
auto basic_sync_update() -> void;

auto basic_sync_is_active() -> bool;
auto basic_sync_get_config() -> const BasicSyncConfig_t&;

auto basic_sync_export_to_string(BasicLineMode_t mode) -> std::string;
auto basic_sync_import_from_string(const std::string& text,
                                   BasicLineMode_t mode) -> bool;

auto basic_sync_export_file() -> bool;
auto basic_sync_import_file() -> bool;
