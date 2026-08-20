// SPDX-License-Identifier: GPL-2.0-only
#include "core/Registry.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,misc-include-cleaner,cppcoreguidelines-owning-memory,google-runtime-int,google-readability-braces-around-statements,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-avoid-do-while,cppcoreguidelines-init-variables,bugprone-easily-swappable-parameters):
// Core configuration and registry persistence manager
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
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

auto Configuration_t::instance() -> Configuration_t& {
  static Configuration_t instance;
  return instance;
}

auto Configuration_t::set_path(const std::string& path) -> void {
  path_ = path;
}

auto Configuration_t::load(const std::string& path) -> bool {
  path_ = path;
  data_.clear();

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
      data_[current_section][key] = value;
    }
  }
  return true;
}

auto Configuration_t::load_defaults() -> void {
  data_.clear();
  set_int("Configuration", "Computer Emulation", 3);
  set_int("Configuration", "Keyboard Type", 0);
  set_int("Configuration", "Keyboard Rocker Switch", 0);
  set_int("Configuration", "Sound Emulation", 1);
  set_int("Configuration", "Soundcard Type", 2);
  set_int("Configuration", "Joystick 0", 2);
  set_int("Configuration", "Joystick 1", 0);
  set_int("Configuration", "Emulation Speed", 10);
  set_int("Configuration", "Enhance Disk Speed", 1);
  set_int("Configuration", "Video Emulation", 1);
  set_string("Configuration", "Monochrome Color", "#C0C0C0");
  set_int("Configuration", "Mouse in slot 4", 0);
  set_int("Configuration", "Printer idle limit", 10);
  set_int("Configuration", "Append to printer file", 1);
  set_int("Configuration", "Harddisk Enable", 0);
  set_int("Configuration", "Clock Enable", 4);
  set_int("Configuration", "Save State On Exit", 0);
  set_int("Configuration", "Fullscreen", 0);
  set_int("Configuration", "Boot at Startup", 0);
  set_int("Configuration", "Show Leds", 1);
  set_string("Configuration", "Screen factor", "1.0");

  set_string("Slots", "Slot 1", "Parallel Printer");
  set_string("Slots", "Slot 2", "Super Serial Card");
  set_string("Slots", "Slot 3", "None");
  set_string("Slots", "Slot 4", "Mockingboard");
  set_string("Slots", "Slot 5", "Mockingboard");
  set_string("Slots", "Slot 6", "Disk II");
  set_string("Slots", "Slot 7", "Harddisk");

  set_string("Preferences", "FTP Server",
             "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/");
  set_string("Preferences", "FTP ServerHDD",
             "ftp://ftp.apple.asimov.net/pub/apple_II/images/");
  set_string("Preferences", "FTP UserPass", "anonymous:my-mail@mail.com");
}

auto Configuration_t::save() -> bool {
  if (path_.empty()) {
    std::string config_dir = Path::get_user_config_dir();
    Path::EnsureDirExists(config_dir);
    path_ = config_dir + "linapple.conf";
  }

#ifdef REGISTRY_WRITEABLE
  std::ofstream file(path_);
  if (!file.is_open()) return false;

  for (auto const& section : data_) {
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

struct ConfigAlias_t {
  const char* canonical;
  const char* legacy;
};

static constexpr ConfigAlias_t k_config_aliases[] = {
    {"Joystick 0 Index", "Joy0Index"},
    {"Joystick 1 Index", "Joy1Index"},
    {"Joystick 0 Button 1", "Joy0Button1"},
    {"Joystick 0 Button 2", "Joy0Button2"},
    {"Joystick 1 Button 1", "Joy1Button1"},
    {"Joystick 0 Axis 0", "Joy0Axis0"},
    {"Joystick 0 Axis 1", "Joy0Axis1"},
    {"Joystick 1 Axis 0", "Joy1Axis0"},
    {"Joystick 1 Axis 1", "Joy1Axis1"},
    {"Joystick Exit Enable", "JoyExitEnable"},
    {"Joystick Exit Button 0", "JoyExitButton0"},
    {"Joystick Exit Button 1", "JoyExitButton1"},
    {"Mouse in slot 4", "Mouse in slot4"},
    {"Mouse Capture", "MouseCapture"},
};

static auto find_alias(const std::string& key) -> const char* {
  for (const auto& entry : k_config_aliases) {
    if (key == entry.canonical) {
      return entry.legacy;
    }
    if (key == entry.legacy) {
      return entry.canonical;
    }
  }
  return nullptr;
}

auto Configuration_t::get_string(const std::string& section,
                                 const std::string& key,
                                 const std::string& default_value)
    -> std::string {
  if (data_.count(section) && data_[section].count(key)) {
    return data_[section][key];
  }

  for (auto const& s : data_) {
    if (s.second.count(key)) return s.second.at(key);
  }

  const char* alias = find_alias(key);
  if (alias != nullptr) {
    std::string alias_str(alias);
    if (data_.count(section) && data_[section].count(alias_str)) {
      return data_[section][alias_str];
    }
    for (auto const& s : data_) {
      if (s.second.count(alias_str)) return s.second.at(alias_str);
    }
  }

  return default_value;
}

auto Configuration_t::get_int(const std::string& section,
                              const std::string& key, uint32_t default_value)
    -> uint32_t {
  std::string val = get_string(section, key);
  if (val.empty()) return default_value;
  try {
    return std::stoul(val, nullptr, 0);
  } catch (...) {
    return default_value;
  }
}

auto Configuration_t::get_bool(const std::string& section,
                               const std::string& key, bool default_value)
    -> bool {
  std::string val = get_string(section, key);
  if (val.empty()) return default_value;
  std::string low_val = val;
  std::transform(low_val.begin(), low_val.end(), low_val.begin(), ::tolower);
  if (low_val == "true" || low_val == "1" || low_val == "yes") return true;
  if (low_val == "false" || low_val == "0" || low_val == "no") return false;
  return default_value;
}

auto Configuration_t::set_string(const std::string& section,
                                 const std::string& key,
                                 const std::string& value) -> void {
  data_[section][key] = value;
}

auto Configuration_t::set_int(const std::string& section,
                              const std::string& key, uint32_t value) -> void {
  data_[section][key] = std::to_string(value);
}

auto Configuration_t::set_bool(const std::string& section,
                               const std::string& key, bool value) -> void {
  data_[section][key] = value ? "1" : "0";
}

auto config_load_int(const char* section, const char* key, uint32_t* value)
    -> bool {
  if (section == nullptr || key == nullptr || value == nullptr) return false;
  if (Configuration_t::instance().get_string(section, key).empty())
    return false;
  *value = Configuration_t::instance().get_int(section, key, *value);
  return true;
}

auto config_load_bool(const char* section, const char* key, bool* value)
    -> bool {
  if (section == nullptr || key == nullptr || value == nullptr) return false;
  if (Configuration_t::instance().get_string(section, key).empty())
    return false;
  *value = Configuration_t::instance().get_bool(section, key, *value);
  return true;
}

auto config_load_string(const char* section, const char* key,
                        std::string* value) -> bool {
  if (section == nullptr || key == nullptr || value == nullptr) return false;
  std::string s = Configuration_t::instance().get_string(section, key);
  if (s.empty()) return false;
  *value = s;
  return true;
}

auto config_save_int(const char* section, const char* key, uint32_t value)
    -> void {
  if (section == nullptr || key == nullptr) return;
  Configuration_t::instance().set_int(section, key, value);
}

auto config_save_string(const char* section, const char* key, const char* value)
    -> void {
  if (section == nullptr || key == nullptr || value == nullptr) return;
  Configuration_t::instance().set_string(section, key, value);
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
