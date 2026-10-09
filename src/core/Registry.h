// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"

enum AppIntent : uint8_t {
  INTENT_RUN,
  INTENT_DIAGNOSTIC,
  INTENT_HELP,
  INTENT_ERROR,
};

enum TuiRenderMode : uint8_t {
  TUI_RENDER_SMART = 0,
  TUI_RENDER_BLOCK = 1,
};

constexpr size_t argv_extra_max = 64;

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

constexpr const char* cfg_pprinter_filename = "Parallel Printer Filename";
constexpr const char* cfg_printer_append = "Append to printer file";
constexpr const char* cfg_printer_eight_bit = "Printer 8-bit output";
constexpr const char* cfg_serial_port = "Serial Port";
constexpr const char* cfg_serial_switches_1 = "Serial Switches 1";
constexpr const char* cfg_serial_switches_2 = "Serial Switches 2";

constexpr const char* cfg_pdl_xtrim = "PDL X-Trim";
constexpr const char* cfg_pdl_ytrim = "PDL Y-Trim";
constexpr const char* cfg_shift_key_mod = "Shift-key mod";
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

struct Configuration {
  std::string path;

  // Application intent and options
  AppIntent intent = INTENT_RUN;
  std::array<std::array<char, path_max_len>, disk_drive_count> disk_path = {};
  std::array<std::array<char, path_max_len>, 2> harddisk_path = {};
  // Set by the command line alone. harddisk_path is also filled from the
  // saved key, and only a path the user named for this run may install a
  // hard disk the configuration does not define.
  std::array<bool, 2> harddisk_path_from_args = {};
  std::array<char, path_max_len> program_path = {};
  std::array<char, path_max_len> config_path = {};
  std::array<char, path_max_len> snapshot_path = {};
  std::array<char, path_max_len> audio_dump_path = {};
  std::array<char, path_max_len> rom_path = {};

  Apple2Type apple2_type = A2TYPE_APPLE2EENHANCED;
  bool apple2_type_explicit = false;
  bool is_pal = false;
  bool is_pal_explicit = false;
  bool is_fullscreen = false;
  bool is_fullscreen_explicit = false;
  bool is_boot = false;
  bool is_benchmark = false;
  bool is_log = false;
  bool is_verbose = false;
  int caps_lock_mode = -1;

  bool is_list_hardware = false;
  std::array<char, path_max_len> hardware_info_name = {};

  // Test/Diagnostic fields
  std::array<char, path_max_len> test_cpu_file = {};
  uint16_t test_cpu_trap = TRAP_NMOS_DEFAULT;
  std::array<char, path_max_len> debugger_script = {};
  bool disable_debugger = false;

  std::array<char, path_max_len> basic_sync_file = {};
  int basic_line_mode = -1;

  TuiRenderMode tui_render_mode = TUI_RENDER_SMART;
  bool tui_render_mode_explicit = false;

  // Extra args for frontend pass-through
  int argc_extra = 0;
  std::array<const char*, argv_extra_max> argv_extra = {};

  std::map<std::string, std::map<std::string, std::string>> data;

  static auto instance() -> Configuration&;

  auto load(const std::string& config_path) -> bool;
  auto load_defaults() -> void;
  auto save() -> bool;
  auto set_path(const std::string& new_path) -> void;
  auto get_path() const -> const std::string& { return path; }

  auto sync_from_data() -> void;
  auto sync_to_data() -> void;

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) Justification: Section and key are distinct configuration coordinates
  auto get_string(const std::string& section, const std::string& key,
                  const std::string& default_value = "") const -> std::string;
  auto get_int(const std::string& section, const std::string& key,
               uint32_t default_value = 0) const -> uint32_t;
  auto get_bool(const std::string& section, const std::string& key,
                bool default_value = false) const -> bool;
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
                  const char* default_value = "") const -> std::string;
  auto get_int(const char* section, const char* key,
               uint32_t default_value = 0) const -> uint32_t;
  auto get_bool(const char* section, const char* key,
                bool default_value = false) const -> bool;

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) Justification: Section, key, and value are distinct configuration parameters
  auto set_string(const char* section, const char* key, const char* value)
      -> void;
  auto set_int(const char* section, const char* key, uint32_t value) -> void;
  auto set_bool(const char* section, const char* key, bool value) -> void;
};

using AppConfig = Configuration;

auto config_instance() -> Configuration&;
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
