// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <map>
#include <string>

// Configuration Keys
constexpr const char* REGVALUE_COMPUTER_EMULATION = "Computer Emulation";
constexpr const char* REGVALUE_APPLE2_TYPE = "Apple2 Type";
constexpr const char* REGVALUE_SPKR_VOLUME = "Speaker Volume";
constexpr const char* REGVALUE_MB_VOLUME = "Mockingboard Volume";
constexpr const char* REGVALUE_SOUNDCARD_TYPE = "Soundcard Type";
constexpr const char* REGVALUE_KEYB_TYPE = "Keyboard Type";
constexpr const char* REGVALUE_KEYB_CHARSET_SWITCH = "Keyboard Rocker Switch";
constexpr const char* REGVALUE_SAVESTATE_FILENAME = "Save State Filename";
constexpr const char* REGVALUE_SAVE_STATE_ON_EXIT = "Save State On Exit";
constexpr const char* REGVALUE_HDD_ENABLED = "Harddisk Enable";
constexpr const char* REGVALUE_HDD_IMAGE1 = "Harddisk Image 1";
constexpr const char* REGVALUE_HDD_IMAGE2 = "Harddisk Image 2";
constexpr const char* REGVALUE_DISK_IMAGE1 = "Disk Image 1";
constexpr const char* REGVALUE_DISK_IMAGE2 = "Disk Image 2";
constexpr const char* REGVALUE_CLOCK_ENABLED = "Clock Enable";

constexpr const char* REGVALUE_JOY_TYPE1 = "Joystick 0";
constexpr const char* REGVALUE_JOY_TYPE2 = "Joystick 1";
constexpr const char* REGVALUE_JOY_INDEX1 = "Joystick 0 Index";
constexpr const char* REGVALUE_JOY_INDEX2 = "Joystick 1 Index";
constexpr const char* REGVALUE_JOY_BUTTON1_1 = "Joystick 0 Button 1";
constexpr const char* REGVALUE_JOY_BUTTON1_2 = "Joystick 0 Button 2";
constexpr const char* REGVALUE_JOY_BUTTON2_1 = "Joystick 1 Button 1";
constexpr const char* REGVALUE_JOY_AXIS1_0 = "Joystick 0 Axis 0";
constexpr const char* REGVALUE_JOY_AXIS1_1 = "Joystick 0 Axis 1";
constexpr const char* REGVALUE_JOY_AXIS2_0 = "Joystick 1 Axis 0";
constexpr const char* REGVALUE_JOY_AXIS2_1 = "Joystick 1 Axis 1";
constexpr const char* REGVALUE_JOY_EXIT_ENABLE = "Joystick Exit Enable";
constexpr const char* REGVALUE_JOY_EXIT_BUTTON0 = "Joystick Exit Button 0";
constexpr const char* REGVALUE_JOY_EXIT_BUTTON1 = "Joystick Exit Button 1";

constexpr const char* REGVALUE_PPRINTER_FILENAME = "Parallel Printer Filename";
constexpr const char* REGVALUE_PRINTER_APPEND = "Append to printer file";
constexpr const char* REGVALUE_PRINTER_IDLE_LIMIT = "Printer idle limit";

constexpr const char* REGVALUE_PDL_XTRIM = "PDL X-Trim";
constexpr const char* REGVALUE_PDL_YTRIM = "PDL Y-Trim";
constexpr const char* REGVALUE_SCROLLLOCK_TOGGLE = "ScrollLock Toggle";
constexpr const char* REGVALUE_MOUSE_IN_SLOT4 = "Mouse in slot 4";

constexpr const char* REGVALUE_PREF_START_DIR = "Slot 6 Directory";
constexpr const char* REGVALUE_PREF_HDD_START_DIR = "HDV Starting Directory";
constexpr const char* REGVALUE_PREF_SAVESTATE_DIR = "Save State Directory";

constexpr const char* REGVALUE_SHOW_LEDS = "Show Leds";

constexpr const char* REGVALUE_FTP_DIR = "FTP Server";
constexpr const char* REGVALUE_FTP_HDD_DIR = "FTP ServerHDD";

constexpr const char* REGVALUE_FTP_LOCAL_DIR = "FTP Local Dir";
constexpr const char* REGVALUE_FTP_USERPASS = "FTP UserPass";

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

inline auto LOAD(const char* key, uint32_t* value) -> bool {
  return ConfigLoadInt("Configuration", key, value);
}

inline auto LOAD(const char* key, bool* value) -> bool {
  return ConfigLoadBool("Configuration", key, value);
}

inline auto LOAD(const char* key, std::string* value) -> bool {
  return ConfigLoadString("Configuration", key, value);
}

inline void SAVE(const char* key, uint32_t value) {
  ConfigSaveInt("Configuration", key, value);
}

#ifdef __cplusplus
extern "C" {
#endif

auto php_trim(char* c, int len) -> char*;

#ifdef __cplusplus
}
#endif
