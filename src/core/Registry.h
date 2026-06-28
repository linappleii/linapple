// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <map>
#include <string>

class Configuration {
 public:
  static auto Instance() -> Configuration&;

  auto Load(const std::string& path) -> bool;
  auto LoadDefaults() -> void;
  auto Save() -> bool;
  auto SetPath(const std::string& path) -> void;
  auto GetPath() const -> const std::string& { return m_path; }

  auto GetString(const std::string& section, const std::string& key,
                 const std::string& default_value = "") -> std::string;
  auto GetInt(const std::string& section, const std::string& key,
              uint32_t default_value = 0) -> uint32_t;
  auto GetBool(const std::string& section, const std::string& key,
               bool default_value = false) -> bool;

  auto SetString(const std::string& section, const std::string& key,
                 const std::string& value) -> void;
  auto SetInt(const std::string& section, const std::string& key,
              uint32_t value) -> void;
  auto SetBool(const std::string& section, const std::string& key, bool value)
      -> void;

 private:
  Configuration() = default;
  std::string m_path;
  std::map<std::string, std::map<std::string, std::string>> m_data;
};

using Configuration_t = Configuration;

auto config_load_int(const char* section, const char* key, uint32_t* value)
    -> bool;
auto config_load_bool(const char* section, const char* key, bool* value)
    -> bool;
auto config_load_string(const char* section, const char* key,
                        std::string* value) -> bool;
auto config_save_int(const char* section, const char* key, uint32_t value)
    -> void;
auto config_save_string(const char* section, const char* key, const char* value)
    -> void;

auto ConfigLoadInt(const char* section, const char* key, uint32_t* value)
    -> bool;
auto ConfigLoadBool(const char* section, const char* key, bool* value) -> bool;
auto ConfigLoadString(const char* section, const char* key, std::string* value)
    -> bool;
auto ConfigSaveInt(const char* section, const char* key, uint32_t value)
    -> void;
auto ConfigSaveString(const char* section, const char* key, const char* value)
    -> void;

#ifdef __cplusplus
extern "C" {
#endif

auto php_trim(char* c, int len) -> char*;

#ifdef __cplusplus
}
#endif
