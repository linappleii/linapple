// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <map>
#include <string>

constexpr const char* cfg_sec_configuration = "Configuration";
constexpr const char* cfg_sec_slots = "Slots";
constexpr const char* cfg_sec_preferences = "Preferences";

constexpr const char* cfg_computer_emulation = "Computer Emulation";
constexpr const char* cfg_apple2_type = "Apple2 Type";
constexpr const char* cfg_spkr_volume = "Speaker Volume";
constexpr const char* cfg_mb_volume = "Mockingboard Volume";
constexpr const char* cfg_soundcard_type = "Soundcard Type";
constexpr const char* cfg_keyb_type = "Keyboard Type";
constexpr const char* cfg_keyb_charset_switch = "Keyboard Rocker Switch";
constexpr const char* cfg_savestate_filename = "Save State Filename";
constexpr const char* cfg_save_state_on_exit = "Save State On Exit";
constexpr const char* cfg_hdd_enabled = "Harddisk Enable";
constexpr const char* cfg_hdd_image1 = "Harddisk Image 1";
constexpr const char* cfg_hdd_image2 = "Harddisk Image 2";
constexpr const char* cfg_disk_image1 = "Disk Image 1";
constexpr const char* cfg_disk_image2 = "Disk Image 2";
constexpr const char* cfg_slot6_autoload = "Slot 6 Autoload";

constexpr const char* cfg_joy_type1 = "Joystick 0";
constexpr const char* cfg_joy_type2 = "Joystick 1";
constexpr const char* cfg_joy_index1 = "Joystick 0 Index";
constexpr const char* cfg_joy_index2 = "Joystick 1 Index";
constexpr const char* cfg_joy_button1_1 = "Joystick 0 Button 1";
constexpr const char* cfg_joy_button1_2 = "Joystick 0 Button 2";
constexpr const char* cfg_joy_button2_1 = "Joystick 1 Button 1";
constexpr const char* cfg_joy_axis1_0 = "Joystick 0 Axis 0";
constexpr const char* cfg_joy_axis1_1 = "Joystick 0 Axis 1";
constexpr const char* cfg_joy_axis2_0 = "Joystick 1 Axis 0";
constexpr const char* cfg_joy_axis2_1 = "Joystick 1 Axis 1";
constexpr const char* cfg_joy_exit_enable = "Joystick Exit Enable";
constexpr const char* cfg_joy_exit_button0 = "Joystick Exit Button 0";
constexpr const char* cfg_joy_exit_button1 = "Joystick Exit Button 1";

constexpr const char* cfg_pprinter_filename = "Parallel Printer Filename";
constexpr const char* cfg_printer_append = "Append to printer file";
constexpr const char* cfg_printer_eight_bit = "Printer 8-bit output";

constexpr const char* cfg_pdl_xtrim = "PDL X-Trim";
constexpr const char* cfg_pdl_ytrim = "PDL Y-Trim";
constexpr const char* cfg_scrolllock_toggle = "ScrollLock Toggle";
constexpr const char* cfg_mouse_in_slot4 = "Mouse in slot 4";
constexpr const char* cfg_mouse_capture = "Mouse Capture";
constexpr const char* cfg_basic_sync_file = "Basic Live Sync File";
constexpr const char* cfg_basic_line_mode = "Basic Line Numbering";

constexpr const char* cfg_pref_start_dir = "Slot 6 Directory";
constexpr const char* cfg_pref_hdd_start_dir = "HDV Starting Directory";
constexpr const char* cfg_pref_savestate_dir = "Save State Directory";

constexpr const char* cfg_show_leds = "Show Leds";
constexpr const char* cfg_disable_debugger = "Disable Debugger";
constexpr const char* cfg_tui_render_mode = "TUI Render Mode";

constexpr const char* cfg_ftp_dir = "FTP Server";
constexpr const char* cfg_ftp_hdd_dir = "FTP ServerHDD";

constexpr const char* cfg_ftp_local_dir = "FTP Local Dir";
constexpr const char* cfg_ftp_userpass = "FTP UserPass";

// Backward-compatible aliases for legacy REGVALUE_* constants
constexpr const char* REGVALUE_COMPUTER_EMULATION = cfg_computer_emulation;
constexpr const char* REGVALUE_APPLE2_TYPE = cfg_apple2_type;
constexpr const char* REGVALUE_SPKR_VOLUME = cfg_spkr_volume;
constexpr const char* REGVALUE_MB_VOLUME = cfg_mb_volume;
constexpr const char* REGVALUE_SOUNDCARD_TYPE = cfg_soundcard_type;
constexpr const char* REGVALUE_KEYB_TYPE = cfg_keyb_type;
constexpr const char* REGVALUE_KEYB_CHARSET_SWITCH = cfg_keyb_charset_switch;
constexpr const char* REGVALUE_SAVESTATE_FILENAME = cfg_savestate_filename;
constexpr const char* REGVALUE_SAVE_STATE_ON_EXIT = cfg_save_state_on_exit;
constexpr const char* REGVALUE_HDD_ENABLED = cfg_hdd_enabled;
constexpr const char* REGVALUE_HDD_IMAGE1 = cfg_hdd_image1;
constexpr const char* REGVALUE_HDD_IMAGE2 = cfg_hdd_image2;
constexpr const char* REGVALUE_DISK_IMAGE1 = cfg_disk_image1;
constexpr const char* REGVALUE_DISK_IMAGE2 = cfg_disk_image2;
constexpr const char* REGVALUE_SLOT6_AUTOLOAD = cfg_slot6_autoload;

constexpr const char* REGVALUE_JOY_TYPE1 = cfg_joy_type1;
constexpr const char* REGVALUE_JOY_TYPE2 = cfg_joy_type2;
constexpr const char* REGVALUE_JOY_INDEX1 = cfg_joy_index1;
constexpr const char* REGVALUE_JOY_INDEX2 = cfg_joy_index2;
constexpr const char* REGVALUE_JOY_BUTTON1_1 = cfg_joy_button1_1;
constexpr const char* REGVALUE_JOY_BUTTON1_2 = cfg_joy_button1_2;
constexpr const char* REGVALUE_JOY_BUTTON2_1 = cfg_joy_button2_1;
constexpr const char* REGVALUE_JOY_AXIS1_0 = cfg_joy_axis1_0;
constexpr const char* REGVALUE_JOY_AXIS1_1 = cfg_joy_axis1_1;
constexpr const char* REGVALUE_JOY_AXIS2_0 = cfg_joy_axis2_0;
constexpr const char* REGVALUE_JOY_AXIS2_1 = cfg_joy_axis2_1;
constexpr const char* REGVALUE_JOY_EXIT_ENABLE = cfg_joy_exit_enable;
constexpr const char* REGVALUE_JOY_EXIT_BUTTON0 = cfg_joy_exit_button0;
constexpr const char* REGVALUE_JOY_EXIT_BUTTON1 = cfg_joy_exit_button1;

constexpr const char* REGVALUE_PPRINTER_FILENAME = cfg_pprinter_filename;
constexpr const char* REGVALUE_PRINTER_APPEND = cfg_printer_append;
constexpr const char* REGVALUE_PRINTER_EIGHT_BIT = cfg_printer_eight_bit;

constexpr const char* REGVALUE_PDL_XTRIM = cfg_pdl_xtrim;
constexpr const char* REGVALUE_PDL_YTRIM = cfg_pdl_ytrim;
constexpr const char* REGVALUE_SCROLLLOCK_TOGGLE = cfg_scrolllock_toggle;
constexpr const char* REGVALUE_MOUSE_IN_SLOT4 = cfg_mouse_in_slot4;
constexpr const char* REGVALUE_MOUSE_CAPTURE = cfg_mouse_capture;
constexpr const char* REGVALUE_BASIC_SYNC_FILE = cfg_basic_sync_file;
constexpr const char* REGVALUE_BASIC_LINE_MODE = cfg_basic_line_mode;

constexpr const char* REGVALUE_PREF_START_DIR = cfg_pref_start_dir;
constexpr const char* REGVALUE_PREF_HDD_START_DIR = cfg_pref_hdd_start_dir;
constexpr const char* REGVALUE_PREF_SAVESTATE_DIR = cfg_pref_savestate_dir;

constexpr const char* REGVALUE_SHOW_LEDS = cfg_show_leds;
constexpr const char* REGVALUE_DISABLE_DEBUGGER = cfg_disable_debugger;
constexpr const char* REGVALUE_TUI_RENDER_MODE = cfg_tui_render_mode;

constexpr const char* REGVALUE_FTP_DIR = cfg_ftp_dir;
constexpr const char* REGVALUE_FTP_HDD_DIR = cfg_ftp_hdd_dir;

constexpr const char* REGVALUE_FTP_LOCAL_DIR = cfg_ftp_local_dir;
constexpr const char* REGVALUE_FTP_USERPASS = cfg_ftp_userpass;

struct Configuration_t {
  std::string path;
  std::map<std::string, std::map<std::string, std::string>> data;

  static auto instance() -> Configuration_t&;

  auto load(const std::string& config_path) -> bool;
  auto load_defaults() -> void;
  auto save() -> bool;
  auto set_path(const std::string& new_path) -> void;
  auto get_path() const -> const std::string& { return path; }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) Justification: Section and key are distinct configuration coordinates
  auto get_string(const std::string& section, const std::string& key,
                  const std::string& default_value = "") -> std::string;
  auto get_int(const std::string& section, const std::string& key,
               uint32_t default_value = 0) -> uint32_t;
  auto get_bool(const std::string& section, const std::string& key,
                bool default_value = false) -> bool;
  auto get_section(const std::string& section) const
      -> const std::map<std::string, std::string>*;

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) Justification: Section, key, and value are distinct configuration parameters
  auto set_string(const std::string& section, const std::string& key,
                  const std::string& value) -> void;
  auto set_int(const std::string& section, const std::string& key,
               uint32_t value) -> void;
  auto set_bool(const std::string& section, const std::string& key, bool value)
      -> void;

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) Justification: Section and key are distinct configuration coordinates
  auto get_string(const char* section, const char* key,
                  const char* default_value = "") -> std::string;
  auto get_int(const char* section, const char* key, uint32_t default_value = 0)
      -> uint32_t;
  auto get_bool(const char* section, const char* key,
                bool default_value = false) -> bool;

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) Justification: Section, key, and value are distinct configuration parameters
  auto set_string(const char* section, const char* key, const char* value)
      -> void;
  auto set_int(const char* section, const char* key, uint32_t value) -> void;
  auto set_bool(const char* section, const char* key, bool value) -> void;
};

auto config_instance() -> Configuration_t&;
auto config_load_file(const char* path) -> bool;
auto config_save_file() -> bool;
auto config_load_defaults() -> void;
auto config_set_path(const char* path) -> void;
auto config_get_path() -> const std::string&;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) Justification: Section and key are distinct configuration coordinates
auto config_get_string(const char* section, const char* key,
                       const char* default_value = "") -> std::string;
auto config_get_int(const char* section, const char* key,
                    uint32_t default_value = 0) -> uint32_t;
auto config_get_bool(const char* section, const char* key,
                     bool default_value = false) -> bool;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) Justification: Section, key, and value are distinct configuration parameters
auto config_set_string(const char* section, const char* key, const char* value)
    -> void;
auto config_set_int(const char* section, const char* key, uint32_t value)
    -> void;
auto config_set_bool(const char* section, const char* key, bool value) -> void;

auto config_load_int(const char* section, const char* key, uint32_t* value)
    -> bool;
auto config_load_bool(const char* section, const char* key, bool* value)
    -> bool;
auto config_load_string(const char* section, const char* key,
                        std::string* value) -> bool;
auto config_save_int(const char* section, const char* key, uint32_t value)
    -> void;
auto config_save_bool(const char* section, const char* key, bool value) -> void;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) Justification: Section, key, and value are distinct configuration parameters
auto config_save_string(const char* section, const char* key, const char* value)
    -> void;

inline auto load(const char* key, uint32_t* value) -> bool {
  return config_load_int(cfg_sec_configuration, key, value);
}

inline auto load(const char* key, bool* value) -> bool {
  return config_load_bool(cfg_sec_configuration, key, value);
}

inline auto load(const char* key, std::string* value) -> bool {
  return config_load_string(cfg_sec_configuration, key, value);
}

inline auto save(const char* key, uint32_t value) -> void {
  config_save_int(cfg_sec_configuration, key, value);
}

inline auto save(const char* key, int value) -> void {
  config_save_int(cfg_sec_configuration, key, static_cast<uint32_t>(value));
}

inline auto save(const char* key, bool value) -> void {
  config_save_bool(cfg_sec_configuration, key, value);
}

inline auto save(const char* key, const char* value) -> void {
  config_save_string(cfg_sec_configuration, key, value);
}

inline auto save(const char* key, const std::string& value) -> void {
  config_save_string(cfg_sec_configuration, key, value.c_str());
}
