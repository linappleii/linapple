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

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

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
  util_safe_strcpy(config_path.data(), path.c_str(), path_max_len);
}

auto Configuration_t::sync_from_data() -> void {
  if (!apple2_type_explicit) {
    std::string emul_str =
        get_string(cfg_sec_configuration, cfg_computer_emulation);
    if (emul_str.empty()) {
      emul_str = get_string(cfg_sec_preferences, cfg_computer_emulation);
    }
    if (!emul_str.empty()) {
      try {
        uint32_t emul = std::stoul(emul_str, nullptr, 0);
        switch (emul) {
          case 0:
            apple2_type = A2TYPE_APPLE2;
            break;
          case 1:
            apple2_type = A2TYPE_APPLE2PLUS;
            break;
          case 2:
            apple2_type = A2TYPE_APPLE2E;
            break;
          case 3:
          default:
            apple2_type = A2TYPE_APPLE2EENHANCED;
            break;
        }
      } catch (...) {
      }
    }
  }

  if (!is_fullscreen_explicit) {
    std::string fs_str = get_string(cfg_sec_configuration, "Fullscreen");
    if (!fs_str.empty()) {
      is_fullscreen = (fs_str == "1" || fs_str == "true" || fs_str == "yes");
    }
  }

  if (!is_pal_explicit) {
    std::string vid_str = get_string(cfg_sec_configuration, "Video Emulation");
    if (!vid_str.empty()) {
      try {
        uint32_t vid = std::stoul(vid_str, nullptr, 0);
        is_pal = (vid == 2);
      } catch (...) {
      }
    }
  }

  if (disk_path.at(0).at(0) == '\0') {
    std::string d1 = get_string(cfg_sec_slots, cfg_disk_image1);
    if (d1.empty()) d1 = get_string(cfg_sec_configuration, cfg_disk_image1);
    if (d1.empty()) d1 = get_string(cfg_sec_preferences, cfg_disk_image1);
    if (!d1.empty()) {
      util_safe_strcpy(disk_path.at(0).data(), d1.c_str(), path_max_len);
    }
  }
  if (disk_path.at(1).at(0) == '\0') {
    std::string d2 = get_string(cfg_sec_slots, cfg_disk_image2);
    if (d2.empty()) d2 = get_string(cfg_sec_configuration, cfg_disk_image2);
    if (d2.empty()) d2 = get_string(cfg_sec_preferences, cfg_disk_image2);
    if (!d2.empty()) {
      util_safe_strcpy(disk_path.at(1).data(), d2.c_str(), path_max_len);
    }
  }

  if (harddisk_path.at(0).at(0) == '\0') {
    std::string hd1 = get_string(cfg_sec_preferences, cfg_hdd_image1);
    if (hd1.empty()) hd1 = get_string(cfg_sec_configuration, cfg_hdd_image1);
    if (!hd1.empty()) {
      util_safe_strcpy(harddisk_path.at(0).data(), hd1.c_str(), path_max_len);
    }
  }
  if (harddisk_path.at(1).at(0) == '\0') {
    std::string hd2 = get_string(cfg_sec_preferences, cfg_hdd_image2);
    if (hd2.empty()) hd2 = get_string(cfg_sec_configuration, cfg_hdd_image2);
    if (!hd2.empty()) {
      util_safe_strcpy(harddisk_path.at(1).data(), hd2.c_str(), path_max_len);
    }
  }

  if (snapshot_path.at(0) == '\0') {
    std::string snap =
        get_string(cfg_sec_configuration, cfg_savestate_filename);
    if (!snap.empty()) {
      util_safe_strcpy(snapshot_path.data(), snap.c_str(), path_max_len);
    }
  }

  if (basic_sync_file.at(0) == '\0') {
    std::string sync_f = get_string(cfg_sec_configuration, cfg_basic_sync_file);
    if (!sync_f.empty()) {
      util_safe_strcpy(basic_sync_file.data(), sync_f.c_str(), path_max_len);
    }
  }
  if (basic_line_mode < 0) {
    std::string bmode_str =
        get_string(cfg_sec_configuration, cfg_basic_line_mode);
    if (!bmode_str.empty()) {
      try {
        basic_line_mode = static_cast<int>(std::stoul(bmode_str, nullptr, 0));
      } catch (...) {
      }
    }
  }

  if (!tui_render_mode_explicit) {
    std::string rmode = get_string(cfg_sec_configuration, cfg_tui_render_mode);
    if (!rmode.empty()) {
      if (rmode == "block" || rmode == "simple") {
        tui_render_mode = TUI_RENDER_BLOCK;
      } else if (rmode == "smart" || rmode == "shape") {
        tui_render_mode = TUI_RENDER_SMART;
      }
    }
  }

  std::string dbg_str = get_string(cfg_sec_configuration, cfg_disable_debugger);
  if (!dbg_str.empty()) {
    bool dbg = (dbg_str == "1" || dbg_str == "true" || dbg_str == "yes");
    disable_debugger = disable_debugger || dbg;
  }

  if (caps_lock_mode < 0) {
    std::string cmode_str = get_string("Keyboard", "Caps Lock Mode");
    if (!cmode_str.empty()) {
      try {
        caps_lock_mode = static_cast<int>(std::stoul(cmode_str, nullptr, 0));
      } catch (...) {
      }
    }
  }
}

auto Configuration_t::sync_to_data() -> void {
  int emul_val = 3;
  switch (apple2_type) {
    case A2TYPE_APPLE2:
      emul_val = 0;
      break;
    case A2TYPE_APPLE2PLUS:
      emul_val = 1;
      break;
    case A2TYPE_APPLE2E:
      emul_val = 2;
      break;
    case A2TYPE_APPLE2EENHANCED:
    default:
      emul_val = 3;
      break;
  }
  data[cfg_sec_configuration][cfg_computer_emulation] =
      std::to_string(emul_val);

  data[cfg_sec_configuration]["Fullscreen"] = is_fullscreen ? "1" : "0";
  data[cfg_sec_configuration]["Video Emulation"] = is_pal ? "2" : "1";

  if (disk_path.at(0).at(0) != '\0') {
    data[cfg_sec_slots][cfg_disk_image1] = disk_path.at(0).data();
  }
  if (disk_path.at(1).at(0) != '\0') {
    data[cfg_sec_slots][cfg_disk_image2] = disk_path.at(1).data();
  }

  if (harddisk_path.at(0).at(0) != '\0') {
    data[cfg_sec_preferences][cfg_hdd_image1] = harddisk_path.at(0).data();
    data[cfg_sec_preferences][cfg_hdd_enabled] = "1";
  }
  if (harddisk_path.at(1).at(0) != '\0') {
    data[cfg_sec_preferences][cfg_hdd_image2] = harddisk_path.at(1).data();
  }

  if (snapshot_path.at(0) != '\0') {
    data[cfg_sec_configuration][cfg_savestate_filename] = snapshot_path.data();
  }

  if (disable_debugger) {
    data[cfg_sec_configuration][cfg_disable_debugger] = "1";
  }

  if (basic_sync_file.at(0) != '\0') {
    data[cfg_sec_configuration][cfg_basic_sync_file] = basic_sync_file.data();
  }
  if (basic_line_mode >= 0) {
    data[cfg_sec_configuration][cfg_basic_line_mode] =
        std::to_string(basic_line_mode);
  }

  if (tui_render_mode_explicit) {
    data[cfg_sec_configuration][cfg_tui_render_mode] =
        (tui_render_mode == TUI_RENDER_BLOCK) ? "block" : "smart";
  }
}

auto Configuration_t::load(const std::string& config_path) -> bool {
  std::ifstream file(config_path);
  if (!file.is_open()) {
    return false;
  }

  path = config_path;
  util_safe_strcpy(this->config_path.data(), path.c_str(), path_max_len);
  data.clear();

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
  sync_from_data();
  return true;
}

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) Justification: Default configuration register values and slot assignments
auto Configuration_t::load_defaults() -> void {
  std::string saved_path = path;
  std::array<char, path_max_len> saved_config_path = config_path;
  *this = Configuration_t{};
  path = std::move(saved_path);
  config_path = saved_config_path;
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
  sync_from_data();
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

auto Configuration_t::save() -> bool {
  if (path.empty()) {
    if (this->config_path.at(0) != '\0') {
      path = this->config_path.data();
    } else {
      std::string config_dir = Path::get_user_config_dir();
      Path::ensure_dir_exists(config_dir);
      path = config_dir + "linapple.conf";
      util_safe_strcpy(this->config_path.data(), path.c_str(), path_max_len);
    }
  }

  sync_to_data();

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
  if (key == cfg_disk_image1) {
    util_safe_strcpy(disk_path.at(0).data(), value.c_str(), path_max_len);
  } else if (key == cfg_disk_image2) {
    util_safe_strcpy(disk_path.at(1).data(), value.c_str(), path_max_len);
  } else if (key == cfg_hdd_image1 || key == "HDV Image 1") {
    util_safe_strcpy(harddisk_path.at(0).data(), value.c_str(), path_max_len);
  } else if (key == cfg_hdd_image2 || key == "HDV Image 2") {
    util_safe_strcpy(harddisk_path.at(1).data(), value.c_str(), path_max_len);
  } else if (key == cfg_savestate_filename) {
    util_safe_strcpy(snapshot_path.data(), value.c_str(), path_max_len);
  } else if (key == cfg_basic_sync_file || key == "BasicLiveSyncFile") {
    util_safe_strcpy(basic_sync_file.data(), value.c_str(), path_max_len);
  } else if (key == cfg_tui_render_mode) {
    if (value == "block" || value == "simple") {
      tui_render_mode = TUI_RENDER_BLOCK;
      tui_render_mode_explicit = true;
    } else if (value == "smart" || value == "shape") {
      tui_render_mode = TUI_RENDER_SMART;
      tui_render_mode_explicit = true;
    }
  }
}

auto Configuration_t::set_int(const std::string& section,
                              const std::string& key, uint32_t value) -> void {
  data[section][key] = std::to_string(value);
  if (key == cfg_computer_emulation) {
    switch (value) {
      case 0:
        apple2_type = A2TYPE_APPLE2;
        break;
      case 1:
        apple2_type = A2TYPE_APPLE2PLUS;
        break;
      case 2:
        apple2_type = A2TYPE_APPLE2E;
        break;
      case 3:
      default:
        apple2_type = A2TYPE_APPLE2EENHANCED;
        break;
    }
  } else if (key == "Fullscreen") {
    is_fullscreen = (value != 0);
  } else if (key == "Video Emulation") {
    is_pal = (value == 2);
  } else if (key == cfg_disable_debugger) {
    disable_debugger = (value != 0);
  } else if (key == cfg_basic_line_mode || key == "BasicLineNumbering") {
    basic_line_mode = static_cast<int>(value);
  } else if (key == "Caps Lock Mode") {
    caps_lock_mode = static_cast<int>(value);
  }
}

auto Configuration_t::set_bool(const std::string& section,
                               const std::string& key, bool value) -> void {
  data[section][key] = value ? "1" : "0";
  if (key == "Fullscreen") {
    is_fullscreen = value;
  } else if (key == cfg_disable_debugger) {
    disable_debugger = value;
  } else if (key == "Video Emulation") {
    is_pal = value;
  }
}

auto Configuration_t::set_string(const char* section, const char* key,
                                 const char* value) -> void {
  if (section == nullptr || key == nullptr || value == nullptr) return;
  set_string(std::string(section), std::string(key), std::string(value));
}

auto Configuration_t::set_int(const char* section, const char* key,
                              uint32_t value) -> void {
  if (section == nullptr || key == nullptr) return;
  set_int(std::string(section), std::string(key), value);
}

auto Configuration_t::set_bool(const char* section, const char* key, bool value)
    -> void {
  if (section == nullptr || key == nullptr) return;
  set_bool(std::string(section), std::string(key), value);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

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
