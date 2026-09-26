// SPDX-License-Identifier: GPL-2.0-only
#include "core/Registry.h"

// Core configuration and registry persistence manager
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <ostream>
#include <string>
#include <utility>

#include "core/Util_Path.h"

static auto trim(const std::string& s) -> std::string {
  auto start = s.begin();
  while (start != s.end() && std::isspace(static_cast<uint8_t>(*start))) {
    start++;
  }
  if (start == s.end()) return "";
  auto end = s.end() - 1;
  while (end != start && std::isspace(static_cast<uint8_t>(*end))) {
    end--;
  }
  return {start, end + 1};
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

auto Configuration_t::set_path(const std::string& new_path) -> void {
  path = new_path;
}

auto Configuration_t::load(const std::string& config_path) -> bool {
  path = config_path;
  data.clear();

  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }

  std::string line;
  std::string current_section = "Configuration";
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line.front() == '#') continue;

    if (line.length() >= 2 && line.front() == '[' && line.back() == ']') {
      current_section = line.substr(1, line.length() - 2);
      continue;
    }

    size_t pos = line.find('=');
    if (pos != std::string::npos) {
      std::string key = trim(line.substr(0, pos));
      std::string value = unquote(trim(line.substr(pos + 1)));
      data[current_section][key] = value;
    }
  }
  return true;
}

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) Justification: Default configuration register values and slot assignments
auto Configuration_t::load_defaults() -> void {
  data.clear();
  set_int(cfg_sec_configuration, cfg_computer_emulation, 3);
  set_int(cfg_sec_configuration, cfg_keyb_type, 0);
  set_int(cfg_sec_configuration, cfg_keyb_charset_switch, 0);
  set_int(cfg_sec_configuration, "Sound Emulation", 1);
  set_int(cfg_sec_configuration, cfg_soundcard_type, 2);
  set_int(cfg_sec_configuration, cfg_joy_type1, 2);
  set_int(cfg_sec_configuration, cfg_joy_type2, 0);
  set_int(cfg_sec_configuration, "Emulation Speed", 10);
  set_int(cfg_sec_configuration, "Disk Turbo", 1);
  set_int(cfg_sec_configuration, "Video Emulation", 1);
  set_string(cfg_sec_configuration, "Monochrome Color", "#C0C0C0");
  set_int(cfg_sec_configuration, cfg_mouse_in_slot4, 0);
  set_int(cfg_sec_configuration, cfg_printer_append, 1);
  set_int(cfg_sec_configuration, cfg_printer_eight_bit, 0);
  set_int(cfg_sec_configuration, cfg_hdd_enabled, 0);
  set_int(cfg_sec_configuration, cfg_save_state_on_exit, 0);
  set_int(cfg_sec_configuration, "Fullscreen", 0);
  set_int(cfg_sec_configuration, "Boot at Startup", 0);
  set_int(cfg_sec_configuration, cfg_slot6_autoload, 0);
  set_int(cfg_sec_configuration, cfg_show_leds, 1);
  set_string(cfg_sec_configuration, "Screen factor", "1.0");
  set_string(cfg_sec_configuration, cfg_basic_sync_file, "");
  set_int(cfg_sec_configuration, cfg_basic_line_mode, 0);

  set_string(cfg_sec_slots, "Slot 1", "Parallel Printer");
  set_string(cfg_sec_slots, "Slot 2", "Super Serial Card");
  set_string(cfg_sec_slots, "Slot 3", "None");
  set_string(cfg_sec_slots, "Slot 4", "Mockingboard");
  set_string(cfg_sec_slots, "Slot 5", "Mockingboard");
  set_string(cfg_sec_slots, "Slot 6", "Disk II");
  set_string(cfg_sec_slots, "Slot 7", "Harddisk");

  set_string(cfg_sec_preferences, cfg_ftp_dir,
             "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/");
  set_string(cfg_sec_preferences, cfg_ftp_hdd_dir,
             "ftp://ftp.apple.asimov.net/pub/apple_II/images/");
  set_string(cfg_sec_preferences, cfg_ftp_userpass,
             "anonymous:my-mail@mail.com");
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

auto Configuration_t::save() -> bool {
  if (path.empty()) {
    std::string config_dir = Path::get_user_config_dir();
    Path::ensure_dir_exists(config_dir);
    path = config_dir + "linapple.conf";
  }

#ifdef REGISTRY_WRITEABLE
  std::ofstream file(path);
  if (!file.is_open()) return false;

  for (auto const& section : data) {
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

static constexpr std::array<ConfigAlias_t, 16> config_aliases = {{
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
    {"Basic Live Sync File", "BasicLiveSyncFile"},
    {"Basic Line Numbering", "BasicLineNumbering"},
}};

static auto find_alias(const std::string& key) -> const char* {
  for (const auto& entry : config_aliases) {
    if (key == entry.canonical) {
      return entry.legacy;
    }
    if (key == entry.legacy) {
      return entry.canonical;
    }
  }
  return nullptr;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) Justification: Section, key, and default value are distinct configuration query arguments
auto Configuration_t::get_string(const std::string& section,
                                 const std::string& key,
                                 const std::string& default_value)
    -> std::string {
  auto sec_it = data.find(section);
  if (sec_it != data.end()) {
    auto key_it = sec_it->second.find(key);
    if (key_it != sec_it->second.end()) {
      return key_it->second;
    }
  }

  for (auto const& s : data) {
    auto key_it = s.second.find(key);
    if (key_it != s.second.end()) return key_it->second;
  }

  const char* alias = find_alias(key);
  if (alias != nullptr) {
    if (sec_it != data.end()) {
      auto alias_it = sec_it->second.find(alias);
      if (alias_it != sec_it->second.end()) {
        return alias_it->second;
      }
    }
    for (auto const& s : data) {
      auto alias_it = s.second.find(alias);
      if (alias_it != s.second.end()) return alias_it->second;
    }
  }

  return default_value;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

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

// NOLINTBEGIN(bugprone-easily-swappable-parameters) Justification: Section, key, and default value are distinct configuration query arguments
auto Configuration_t::get_string(const char* section, const char* key,
                                 const char* default_value) -> std::string {
  if (section == nullptr || key == nullptr) {
    return default_value != nullptr ? default_value : "";
  }
  return get_string(std::string(section), std::string(key),
                    default_value != nullptr ? default_value : "");
}
// NOLINTEND(bugprone-easily-swappable-parameters)

auto Configuration_t::get_int(const char* section, const char* key,
                              uint32_t default_value) -> uint32_t {
  if (section == nullptr || key == nullptr) return default_value;
  return get_int(std::string(section), std::string(key), default_value);
}

auto Configuration_t::get_bool(const char* section, const char* key,
                               bool default_value) -> bool {
  if (section == nullptr || key == nullptr) return default_value;
  return get_bool(std::string(section), std::string(key), default_value);
}

auto Configuration_t::get_section(const std::string& section) const
    -> const std::map<std::string, std::string>* {
  auto it = data.find(section);
  if (it != data.end()) {
    return &it->second;
  }
  return nullptr;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) Justification: Section, key, and value are distinct configuration parameters
auto Configuration_t::set_string(const std::string& section,
                                 const std::string& key,
                                 const std::string& value) -> void {
  data[section][key] = value;
}

auto Configuration_t::set_int(const std::string& section,
                              const std::string& key, uint32_t value) -> void {
  data[section][key] = std::to_string(value);
}

auto Configuration_t::set_bool(const std::string& section,
                               const std::string& key, bool value) -> void {
  data[section][key] = value ? "1" : "0";
}

auto Configuration_t::set_string(const char* section, const char* key,
                                 const char* value) -> void {
  if (section == nullptr || key == nullptr || value == nullptr) return;
  data[section][key] = value;
}

auto Configuration_t::set_int(const char* section, const char* key,
                              uint32_t value) -> void {
  if (section == nullptr || key == nullptr) return;
  data[section][key] = std::to_string(value);
}

auto Configuration_t::set_bool(const char* section, const char* key, bool value)
    -> void {
  if (section == nullptr || key == nullptr) return;
  data[section][key] = value ? "1" : "0";
}

auto config_instance() -> Configuration_t& {
  return Configuration_t::instance();
}

auto config_load_file(const char* path) -> bool {
  if (path == nullptr) return false;
  return Configuration_t::instance().load(path);
}

auto config_save_file() -> bool { return Configuration_t::instance().save(); }

auto config_load_defaults() -> void {
  Configuration_t::instance().load_defaults();
}

auto config_set_path(const char* path) -> void {
  if (path == nullptr) return;
  Configuration_t::instance().set_path(path);
}

auto config_get_path() -> const std::string& {
  return Configuration_t::instance().get_path();
}

auto config_get_string(const char* section, const char* key,
                       const char* default_value) -> std::string {
  return Configuration_t::instance().get_string(section, key, default_value);
}

auto config_get_int(const char* section, const char* key,
                    uint32_t default_value) -> uint32_t {
  return Configuration_t::instance().get_int(section, key, default_value);
}

auto config_get_bool(const char* section, const char* key, bool default_value)
    -> bool {
  return Configuration_t::instance().get_bool(section, key, default_value);
}

auto config_set_string(const char* section, const char* key, const char* value)
    -> void {
  if (section == nullptr || key == nullptr || value == nullptr) return;
  Configuration_t::instance().set_string(section, key, value);
}

auto config_set_int(const char* section, const char* key, uint32_t value)
    -> void {
  if (section == nullptr || key == nullptr) return;
  Configuration_t::instance().set_int(section, key, value);
}

auto config_set_bool(const char* section, const char* key, bool value) -> void {
  if (section == nullptr || key == nullptr) return;
  Configuration_t::instance().set_bool(section, key, value);
}

auto config_load_int(const char* section, const char* key, uint32_t* value)
    -> bool {
  if (section == nullptr || key == nullptr || value == nullptr) return false;
  std::string s = Configuration_t::instance().get_string(section, key);
  if (s.empty()) {
    return false;
  }
  try {
    *value = std::stoul(s, nullptr, 0);
    return true;
  } catch (...) {
    return false;
  }
}

auto config_load_bool(const char* section, const char* key, bool* value)
    -> bool {
  if (section == nullptr || key == nullptr || value == nullptr) return false;
  std::string val = Configuration_t::instance().get_string(section, key);
  if (val.empty()) {
    return false;
  }
  std::transform(val.begin(), val.end(), val.begin(), ::tolower);
  if (val == "true" || val == "1" || val == "yes") {
    *value = true;
    return true;
  }
  if (val == "false" || val == "0" || val == "no") {
    *value = false;
    return true;
  }
  return false;
}

auto config_load_string(const char* section, const char* key,
                        std::string* value) -> bool {
  if (section == nullptr || key == nullptr || value == nullptr) return false;
  std::string s = Configuration_t::instance().get_string(section, key);
  if (s.empty()) return false;
  *value = std::move(s);
  return true;
}

auto config_save_int(const char* section, const char* key, uint32_t value)
    -> void {
  if (section == nullptr || key == nullptr) return;
  Configuration_t::instance().set_int(section, key, value);
}

auto config_save_bool(const char* section, const char* key, bool value)
    -> void {
  if (section == nullptr || key == nullptr) return;
  Configuration_t::instance().set_bool(section, key, value);
}

auto config_save_string(const char* section, const char* key, const char* value)
    -> void {
  if (section == nullptr || key == nullptr || value == nullptr) return;
  Configuration_t::instance().set_string(section, key, value);
}
// NOLINTEND(bugprone-easily-swappable-parameters)
