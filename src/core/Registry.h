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
constexpr const char* REGVALUE_MOUSE_CAPTURE = "Mouse Capture";

constexpr const char* REGVALUE_PREF_START_DIR = "Slot 6 Directory";
constexpr const char* REGVALUE_PREF_HDD_START_DIR = "HDV Starting Directory";
constexpr const char* REGVALUE_PREF_SAVESTATE_DIR = "Save State Directory";

constexpr const char* REGVALUE_SHOW_LEDS = "Show Leds";
constexpr const char* REGVALUE_DISABLE_DEBUGGER = "Disable Debugger";

constexpr const char* REGVALUE_FTP_DIR = "FTP Server";
constexpr const char* REGVALUE_FTP_HDD_DIR = "FTP ServerHDD";

constexpr const char* REGVALUE_FTP_LOCAL_DIR = "FTP Local Dir";
constexpr const char* REGVALUE_FTP_USERPASS = "FTP UserPass";

class Configuration_t {
 public:
  static auto instance() -> Configuration_t&;

  auto load(const std::string& path) -> bool;
  auto load_defaults() -> void;
  auto save() -> bool;
  auto set_path(const std::string& path) -> void;
  auto get_path() const -> const std::string& { return path_; }

  auto get_string(const std::string& section, const std::string& key,
                  const std::string& default_value = "") -> std::string;
  auto get_int(const std::string& section, const std::string& key,
               uint32_t default_value = 0) -> uint32_t;
  auto get_bool(const std::string& section, const std::string& key,
                bool default_value = false) -> bool;
  auto get_section(const std::string& section) const
      -> const std::map<std::string, std::string>*;

  auto set_string(const std::string& section, const std::string& key,
                  const std::string& value) -> void;
  auto set_int(const std::string& section, const std::string& key,
               uint32_t value) -> void;
  auto set_bool(const std::string& section, const std::string& key, bool value)
      -> void;

 private:
  Configuration_t() = default;
  std::string path_;
  std::map<std::string, std::map<std::string, std::string>> data_;
};

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

inline auto load(const char* key, uint32_t* value) -> bool {
  return config_load_int("Configuration", key, value);
}

inline auto load(const char* key, bool* value) -> bool {
  return config_load_bool("Configuration", key, value);
}

inline auto load(const char* key, std::string* value) -> bool {
  return config_load_string("Configuration", key, value);
}

inline void save(const char* key, uint32_t value) {
  config_save_int("Configuration", key, value);
}

#ifdef __cplusplus
extern "C" {
#endif

auto php_trim(char* c, int len) -> char*;

#ifdef __cplusplus
}
#endif
