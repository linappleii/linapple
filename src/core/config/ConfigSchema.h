// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Peripheral_Types.h"
#include "core/config/Toml.h"

enum class MachineType_t {
  Apple2 = 0,
  Apple2Plus,
  Apple2JPlus,
  Apple2e,
  Apple2eEnhanced,
  CloneBase64A,
  ClonePravets82,
  ClonePravets8M,
  ClonePravets8C,
  CloneTK3000e
};

enum class VideoStandard_t { NTSC = 0, PAL };

enum class VideoEmulation_t {
  MonochromeCustom = 0,
  ColorStandard,
  ColorTextOptimized,
  ColorTvEmulation,
  ColorHalfShift,
  MonochromeAmber,
  MonochromeGreen,
  MonochromeWhite
};

enum class BasicLineNumbering_t { Explicit = 0, Positional };

enum class ConfigTuiRenderMode_t { Smart = 0, Block };

enum class SoundcardType_t { None = 1, Mockingboard = 2, Phasor = 3 };

enum class ConfigKeyboardLayout_t { Auto = 0, US, UK, French, German, Spanish };

enum class KeyboardRocker_t { US = 0, Local };

enum class KeyMappingMode_t { Symbolic = 0, Positional };

enum class ConfigCapsLockMode_t { Host = 0, Emulated };

enum class QuickSaveModifier_t { Alt = 0, Ctrl, AltCtrl, Disabled };

enum class JoystickMode_t {
  Disabled = 0,
  HostGamepad,
  KeyboardStandard,
  KeyboardCentered,
  Mouse
};

enum class PeripheralCardType_t {
  Empty = 0,
  ParallelPrinter,
  SuperSerial,
  Mockingboard,
  DiskII,
  Harddisk,
  Mouse,
  Clock
};

struct CoreConfig_t {
  MachineType_t machine = MachineType_t::Apple2eEnhanced;
  double emulation_speed = 1.0;
  bool enhance_disk_speed = true;
  bool boot_on_startup = false;
  bool save_state_on_exit = false;
  bool enable_debugger = true;
  std::string basic_sync_file;
  BasicLineNumbering_t basic_line_numbering = BasicLineNumbering_t::Explicit;
};

struct VideoConfig_t {
  VideoStandard_t video_standard = VideoStandard_t::NTSC;
  VideoEmulation_t video_emulation = VideoEmulation_t::ColorStandard;
  std::string monochrome_color = "#C0C0C0";
  double screen_factor = 1.0;
  int screen_width = 0;
  int screen_height = 0;
  bool fullscreen = false;
  bool multithreaded = true;
  bool show_leds = true;
  ConfigTuiRenderMode_t tui_render_mode = ConfigTuiRenderMode_t::Smart;
};

struct FrontendConfig_t {
  double screen_factor = 1.0;
  int screen_width = 0;
  int screen_height = 0;
  bool fullscreen = false;
  bool multithreaded = true;
  bool show_leds = true;
  ConfigTuiRenderMode_t render_mode = ConfigTuiRenderMode_t::Smart;
  bool enable_hotkeys = true;
};

struct AudioConfig_t {
  bool sound_emulation = true;
  int speaker_volume = 50;
  int mockingboard_volume = 50;
  SoundcardType_t soundcard_type = SoundcardType_t::Mockingboard;
};

struct KeyboardConfig_t {
  ConfigKeyboardLayout_t layout = ConfigKeyboardLayout_t::Auto;
  KeyboardRocker_t rocker_switch = KeyboardRocker_t::US;
  KeyMappingMode_t mapping_mode = KeyMappingMode_t::Symbolic;
  ConfigCapsLockMode_t caps_lock_mode = ConfigCapsLockMode_t::Host;
  QuickSaveModifier_t quick_save_modifier = QuickSaveModifier_t::Alt;
  bool enable_hotkeys = true;
  bool scroll_lock_toggle = true;
};

struct JoystickConfig_t {
  JoystickMode_t joy0_mode = JoystickMode_t::KeyboardStandard;
  JoystickMode_t joy1_mode = JoystickMode_t::Disabled;
  int joy0_index = 0;
  int joy1_index = 1;
  int joy0_button1 = 0;
  int joy0_button2 = 1;
  int joy1_button1 = 0;
  int joy1_button2 = 1;
  int joy0_axis0 = 0;
  int joy0_axis1 = 1;
  int joy1_axis0 = 0;
  int joy1_axis1 = 1;
  int pdl_x_trim = 0;
  int pdl_y_trim = 0;
  bool joy_exit_enable = false;
  int joy_exit_button0 = 8;
  int joy_exit_button1 = 9;
  bool mouse_capture = true;
};

struct InputConfig_t {
  ConfigKeyboardLayout_t keyboard_type = ConfigKeyboardLayout_t::Auto;
  KeyboardRocker_t keyboard_rocker = KeyboardRocker_t::US;
  KeyMappingMode_t mapping_mode = KeyMappingMode_t::Symbolic;
  ConfigCapsLockMode_t caps_lock_mode = ConfigCapsLockMode_t::Host;
  QuickSaveModifier_t quick_save_modifier = QuickSaveModifier_t::Alt;
  bool enable_hotkeys = true;
  bool scroll_lock_toggle = true;
  bool mouse_capture = true;

  JoystickMode_t joy0_mode = JoystickMode_t::KeyboardStandard;
  JoystickMode_t joy1_mode = JoystickMode_t::Disabled;
  int joy0_index = 0;
  int joy1_index = 1;
  int joy0_button1 = 0;
  int joy0_button2 = 1;
  int joy1_button1 = 0;
  int joy1_button2 = 1;
  int joy0_axis0 = 0;
  int joy0_axis1 = 1;
  int joy1_axis0 = 0;
  int joy1_axis1 = 1;
  int pdl_x_trim = 0;
  int pdl_y_trim = 0;
  bool joy_exit_enable = false;
  int joy_exit_button0 = 8;
  int joy_exit_button1 = 9;
};

constexpr size_t config_slot_count = 8;

struct SlotsConfig_t {
  std::array<PeripheralCardType_t, config_slot_count> cards = {{
      PeripheralCardType_t::Empty,            // Slot 0 (Motherboard ROM/RAM)
      PeripheralCardType_t::ParallelPrinter,  // Slot 1
      PeripheralCardType_t::SuperSerial,      // Slot 2
      PeripheralCardType_t::Empty,            // Slot 3
      PeripheralCardType_t::Mockingboard,     // Slot 4
      PeripheralCardType_t::Mockingboard,     // Slot 5
      PeripheralCardType_t::DiskII,           // Slot 6
      PeripheralCardType_t::Harddisk          // Slot 7
  }};
};

struct LinAppleConfig_t {
  CoreConfig_t core;
  VideoConfig_t video;
  FrontendConfig_t frontend;
  AudioConfig_t audio;
  InputConfig_t input;
  KeyboardConfig_t keyboard;
  JoystickConfig_t joystick;
  SlotsConfig_t slots;

  // Preserved raw TOML document containing peripheral-specific sections
  // (e.g. [Peripheral.DiskII.Slot6]) or custom user settings.
  std::unique_ptr<TomlDocument_t> raw_doc;
};

struct ConfigValidationError_t {
  std::string section;
  std::string key;
  std::string message;
  bool is_warning = false;

  ConfigValidationError_t() = default;
  ConfigValidationError_t(std::string sec, std::string k, std::string msg,
                          bool warning = false)
      : section(std::move(sec)),
        key(std::move(k)),
        message(std::move(msg)),
        is_warning(warning) {}
};

// --- Enum Conversion Functions ---
auto machine_type_to_string(MachineType_t machine) -> const char*;
auto machine_type_from_string(const std::string& str, MachineType_t* out)
    -> bool;

auto video_standard_to_string(VideoStandard_t standard) -> const char*;
auto video_standard_from_string(const std::string& str, VideoStandard_t* out)
    -> bool;

auto video_emulation_to_string(VideoEmulation_t emu) -> const char*;
auto video_emulation_from_string(const std::string& str, VideoEmulation_t* out)
    -> bool;

auto basic_line_numbering_to_string(BasicLineNumbering_t numbering) -> const
    char*;
auto basic_line_numbering_from_string(const std::string& str,
                                      BasicLineNumbering_t* out) -> bool;

auto tui_render_mode_to_string(ConfigTuiRenderMode_t mode) -> const char*;
auto tui_render_mode_from_string(const std::string& str,
                                 ConfigTuiRenderMode_t* out) -> bool;

auto soundcard_type_to_string(SoundcardType_t type) -> const char*;
auto soundcard_type_from_string(const std::string& str, SoundcardType_t* out)
    -> bool;

auto keyboard_layout_to_string(ConfigKeyboardLayout_t layout) -> const char*;
auto keyboard_layout_from_string(const std::string& str,
                                 ConfigKeyboardLayout_t* out) -> bool;

auto keyboard_rocker_to_string(KeyboardRocker_t rocker) -> const char*;
auto keyboard_rocker_from_string(const std::string& str, KeyboardRocker_t* out)
    -> bool;

auto key_mapping_mode_to_string(KeyMappingMode_t mode) -> const char*;
auto key_mapping_mode_from_string(const std::string& str, KeyMappingMode_t* out)
    -> bool;

auto caps_lock_mode_to_string(ConfigCapsLockMode_t mode) -> const char*;
auto caps_lock_mode_from_string(const std::string& str,
                                ConfigCapsLockMode_t* out) -> bool;

auto quick_save_modifier_to_string(QuickSaveModifier_t mod) -> const char*;
auto quick_save_modifier_from_string(const std::string& str,
                                     QuickSaveModifier_t* out) -> bool;

auto joystick_mode_to_string(JoystickMode_t mode) -> const char*;
auto joystick_mode_from_string(const std::string& str, JoystickMode_t* out)
    -> bool;

auto peripheral_card_to_string(PeripheralCardType_t card) -> const char*;
auto peripheral_card_from_string(const std::string& str,
                                 PeripheralCardType_t* out) -> bool;

// --- Config Lifecycle & Validation Functions ---
auto config_defaults() -> LinAppleConfig_t;

auto config_validate(const LinAppleConfig_t& config,
                     std::vector<ConfigValidationError_t>* out_errors = nullptr)
    -> bool;

auto config_from_toml(
    const TomlDocument_t* doc, LinAppleConfig_t* out_config,
    std::vector<ConfigValidationError_t>* out_errors = nullptr) -> bool;

auto config_to_toml(const LinAppleConfig_t& config)
    -> std::unique_ptr<TomlDocument_t>;

// --- Peripheral Schema Documentation Helpers ---
auto config_get_allowed_cards_list() -> std::string;

auto peripheral_format_option_comment(const PeripheralConfigOption_t& opt)
    -> std::string;

auto peripheral_populate_toml_table(
    TomlDocument_t* doc, const std::string& section_name,
    const char* peripheral_id,
    const std::map<std::string, std::string>& override_values = {}) -> void;
