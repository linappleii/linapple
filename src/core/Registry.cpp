// SPDX-License-Identifier: GPL-2.0-only
#include "core/Registry.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,misc-include-cleaner,cppcoreguidelines-owning-memory,google-runtime-int,google-readability-braces-around-statements,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-avoid-do-while,cppcoreguidelines-init-variables,bugprone-easily-swappable-parameters): Core configuration and registry persistence manager
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include "core/Common.h"
#include "core/Util_Path.h"

static auto trim(const std::string& s) -> std::string {
  auto start = s.begin();
  while (start != s.end() && std::isspace(static_cast<uint8_t>(*start))) {
    start++;
  }
  auto end = s.end();
  if (start == end) return "";
  do {
    end--;
  } while (std::distance(start, end) > 0 &&
           std::isspace(static_cast<uint8_t>(*end)));
  return std::string(start, end + 1);
}

static auto unquote(const std::string& s) -> std::string {
  if (s.length() >= 2 && s.front() == '"' && s.back() == '"') {
    return s.substr(1, s.length() - 2);
  }
  return s;
}

auto Configuration::Instance() -> Configuration& {
  static Configuration instance;
  return instance;
}

auto Configuration::SetPath(const std::string& path) -> void { m_path = path; }

auto Configuration::Load(const std::string& path) -> bool {
  m_path = path;
  m_data.clear();

  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }

  std::string line;
  std::string current_section = "Configuration";
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;

    if (line.length() >= 2 && line[0] == '[' && line.back() == ']') {
      current_section = line.substr(1, line.length() - 2);
      continue;
    }

    size_t pos = line.find('=');
    if (pos != std::string::npos) {
      std::string key = trim(line.substr(0, pos));
      std::string value = unquote(trim(line.substr(pos + 1)));
      m_data[current_section][key] = value;
    }
  }
  return true;
}

auto Configuration::LoadDefaults() -> void {
  m_data.clear();
  SetInt("Configuration", "Computer Emulation", 3);
  SetInt("Configuration", "Keyboard Type", 0);
  SetInt("Configuration", "Keyboard Rocker Switch", 0);
  SetInt("Configuration", "Sound Emulation", 1);
  SetInt("Configuration", "Soundcard Type", 2);
  SetInt("Configuration", "Joystick 0", 2);
  SetInt("Configuration", "Joystick 1", 0);
  SetInt("Configuration", "Emulation Speed", 10);
  SetInt("Configuration", "Enhance Disk Speed", 1);
  SetInt("Configuration", "Video Emulation", 1);
  SetString("Configuration", "Monochrome Color", "#C0C0C0");
  SetInt("Configuration", "Mouse in slot 4", 0);
  SetInt("Configuration", "Printer idle limit", 10);
  SetInt("Configuration", "Append to printer file", 1);
  SetInt("Configuration", "Harddisk Enable", 0);
  SetInt("Configuration", "Clock Enable", 4);
  SetInt("Configuration", "Save State On Exit", 0);
  SetInt("Configuration", "Fullscreen", 0);
  SetInt("Configuration", "Boot at Startup", 0);
  SetInt("Configuration", "Show Leds", 1);
  SetString("Configuration", "Screen factor", "1.0");

  SetString("Slots", "Slot 1", "Parallel Printer");
  SetString("Slots", "Slot 2", "Super Serial Card");
  SetString("Slots", "Slot 3", "None");
  SetString("Slots", "Slot 4", "Mockingboard");
  SetString("Slots", "Slot 5", "Mockingboard");
  SetString("Slots", "Slot 6", "Disk II");
  SetString("Slots", "Slot 7", "Harddisk");

  SetString("Preferences", "FTP Server",
            "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/");
  SetString("Preferences", "FTP ServerHDD",
            "ftp://ftp.apple.asimov.net/pub/apple_II/images/");
  SetString("Preferences", "FTP UserPass", "anonymous:my-mail@mail.com");
}

auto Configuration::Save() -> bool {
  if (m_path.empty()) {
    std::string config_dir = Path::GetUserConfigDir();
    Path::EnsureDirExists(config_dir);
    m_path = config_dir + "linapple.conf";
  }

#ifdef REGISTRY_WRITEABLE
  std::ofstream file(m_path);
  if (!file.is_open()) return false;

  for (auto const& section : m_data) {
    if (section.first != "Default") {
      file << "[" << section.first << "]" << std::endl;
    }
    for (auto const& kv : section.second) {
      file << kv.first << " = " << kv.second << std::endl;
    }
    file << std::endl;
  }
  return true;
#else
  return false;
#endif
}

auto Configuration::GetString(const std::string& section,
                              const std::string& key,
                              const std::string& default_value) -> std::string {
  if (m_data.count(section) && m_data[section].count(key)) {
    return m_data[section][key];
  }

  for (auto const& s : m_data) {
    if (s.second.count(key)) return s.second.at(key);
  }
  return default_value;
}

auto Configuration::GetInt(const std::string& section, const std::string& key,
                           uint32_t default_value) -> uint32_t {
  std::string val = GetString(section, key);
  if (val.empty()) return default_value;
  try {
    return std::stoul(val, nullptr, 0);
  } catch (...) {
    return default_value;
  }
}

auto Configuration::GetBool(const std::string& section, const std::string& key,
                            bool default_value) -> bool {
  std::string val = GetString(section, key);
  if (val.empty()) return default_value;
  std::string low_val = val;
  std::transform(low_val.begin(), low_val.end(), low_val.begin(), ::tolower);
  if (low_val == "true" || low_val == "1" || low_val == "yes") return true;
  if (low_val == "false" || low_val == "0" || low_val == "no") return false;
  return default_value;
}

auto Configuration::SetString(const std::string& section,
                              const std::string& key,
                              const std::string& value) -> void {
  m_data[section][key] = value;
}

auto Configuration::SetInt(const std::string& section, const std::string& key,
                           uint32_t value) -> void {
  m_data[section][key] = std::to_string(value);
}

auto Configuration::SetBool(const std::string& section, const std::string& key,
                            bool value) -> void {
  m_data[section][key] = value ? "1" : "0";
}

auto config_load_int(const char* section, const char* key, uint32_t* value)
    -> bool {
  if (section == nullptr || key == nullptr || value == nullptr) return false;
  if (Configuration::Instance().GetString(section, key).empty()) return false;
  *value = Configuration::Instance().GetInt(section, key, *value);
  return true;
}

auto config_load_bool(const char* section, const char* key, bool* value) -> bool {
  if (section == nullptr || key == nullptr || value == nullptr) return false;
  if (Configuration::Instance().GetString(section, key).empty()) return false;
  *value = Configuration::Instance().GetBool(section, key, *value);
  return true;
}

auto config_load_string(const char* section, const char* key,
                        std::string* value) -> bool {
  if (section == nullptr || key == nullptr || value == nullptr) return false;
  std::string s = Configuration::Instance().GetString(section, key);
  if (s.empty()) return false;
  *value = s;
  return true;
}

auto config_save_int(const char* section, const char* key, uint32_t value)
    -> void {
  if (section == nullptr || key == nullptr) return;
  Configuration::Instance().SetInt(section, key, value);
}

auto config_save_string(const char* section, const char* key,
                        const char* value) -> void {
  if (section == nullptr || key == nullptr || value == nullptr) return;
  Configuration::Instance().SetString(section, key, value);
}

auto ConfigLoadInt(const char* section, const char* key, uint32_t* value)
    -> bool {
  return config_load_int(section, key, value);
}

auto ConfigLoadBool(const char* section, const char* key, bool* value) -> bool {
  return config_load_bool(section, key, value);
}

auto ConfigLoadString(const char* section, const char* key, std::string* value)
    -> bool {
  return config_load_string(section, key, value);
}

auto ConfigSaveInt(const char* section, const char* key, uint32_t value) -> void {
  config_save_int(section, key, value);
}

auto ConfigSaveString(const char* section, const char* key, const char* value)
    -> void {
  config_save_string(section, key, value);
}

auto php_trim(char* c, int len) -> char* {
  if (c == nullptr || len <= 0) {
    return strdup("");
  }
  std::string s(c, static_cast<size_t>(len));
  std::string t = trim(s);
  return strdup(t.c_str());
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,misc-include-cleaner,cppcoreguidelines-owning-memory,google-runtime-int,google-readability-braces-around-statements,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-avoid-do-while,cppcoreguidelines-init-variables,bugprone-easily-swappable-parameters)
