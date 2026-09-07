// SPDX-License-Identifier: GPL-2.0-only
#include "core/config/ConfigSchema.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "core/Util_Path.h"
#include "core/config/ConfigDispatch.h"
#include "core/config/Toml.h"

namespace {

auto normalize_identifier(const std::string& str) -> std::string {
  std::string result;
  result.reserve(str.size());
  for (const char ch : str) {
    if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
      result += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
  }
  return result;
}

auto normalize_machine_name(const std::string& str) -> std::string {
  std::string s = str;
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  size_t pos = 0;
  while ((pos = s.find("][", pos)) != std::string::npos) {
    s.replace(pos, 2, "2");
    pos += 1;
  }
  pos = 0;
  while ((pos = s.find("//", pos)) != std::string::npos) {
    s.replace(pos, 2, "2");
    pos += 1;
  }
  pos = 0;
  while ((pos = s.find('+', pos)) != std::string::npos) {
    s.replace(pos, 1, "plus");
    pos += 4;
  }
  std::string result;
  result.reserve(s.size());
  for (const char ch : s) {
    if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
      result += ch;
    }
  }
  return result;
}

auto is_valid_hex_color(const std::string& str) -> bool {
  if (str.size() != 7 && str.size() != 9) {
    return false;
  }
  if (str[0] != '#') {
    return false;
  }
  for (size_t i = 1; i < str.size(); ++i) {
    if (std::isxdigit(static_cast<unsigned char>(str[i])) == 0) {
      return false;
    }
  }
  return true;
}

}  // namespace

// --- MachineType_t ---
auto machine_type_to_string(MachineType_t machine) -> const char* {
  switch (machine) {
    case MachineType_t::Apple2:
      return "Apple ][";
    case MachineType_t::Apple2Plus:
      return "Apple ][+";
    case MachineType_t::Apple2JPlus:
      return "Apple ][ J-Plus";
    case MachineType_t::Apple2e:
      return "Apple //e";
    case MachineType_t::Apple2eEnhanced:
      return "Apple //e Enhanced";
    case MachineType_t::CloneBase64A:
      return "Base 64A";
    case MachineType_t::ClonePravets82:
      return "Pravets 82";
    case MachineType_t::ClonePravets8M:
      return "Pravets 8M";
    case MachineType_t::ClonePravets8C:
      return "Pravets 8C";
    case MachineType_t::CloneTK3000e:
      return "TK3000 //e";
  }
  return "Apple //e Enhanced";
}

auto machine_type_from_string(const std::string& str, MachineType_t* out)
    -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_machine_name(str);

  if (norm == "apple2eenhanced" || norm == "appleiieenhanced") {
    *out = MachineType_t::Apple2eEnhanced;
    return true;
  }
  if (norm == "apple2e" || norm == "appleiie") {
    *out = MachineType_t::Apple2e;
    return true;
  }
  if (norm == "apple2plus" || norm == "appleiiplus") {
    *out = MachineType_t::Apple2Plus;
    return true;
  }
  if (norm == "apple2" || norm == "appleii") {
    *out = MachineType_t::Apple2;
    return true;
  }
  if (norm == "apple2jplus" || norm == "appleiijplus") {
    *out = MachineType_t::Apple2JPlus;
    return true;
  }
  if (norm == "base64a") {
    *out = MachineType_t::CloneBase64A;
    return true;
  }
  if (norm == "pravets82") {
    *out = MachineType_t::ClonePravets82;
    return true;
  }
  if (norm == "pravets8m") {
    *out = MachineType_t::ClonePravets8M;
    return true;
  }
  if (norm == "pravets8c") {
    *out = MachineType_t::ClonePravets8C;
    return true;
  }
  if (norm == "tk3000e" || norm == "tk30002e" || norm == "tk3000iie") {
    *out = MachineType_t::CloneTK3000e;
    return true;
  }
  return false;
}

// --- VideoStandard_t ---
auto video_standard_to_string(VideoStandard_t standard) -> const char* {
  switch (standard) {
    case VideoStandard_t::NTSC:
      return "NTSC";
    case VideoStandard_t::PAL:
      return "PAL";
  }
  return "NTSC";
}

auto video_standard_from_string(const std::string& str, VideoStandard_t* out)
    -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "ntsc") {
    *out = VideoStandard_t::NTSC;
    return true;
  }
  if (norm == "pal") {
    *out = VideoStandard_t::PAL;
    return true;
  }
  return false;
}

// --- VideoEmulation_t ---
auto video_emulation_to_string(VideoEmulation_t emu) -> const char* {
  switch (emu) {
    case VideoEmulation_t::MonochromeCustom:
      return "Custom Monochrome";
    case VideoEmulation_t::ColorStandard:
      return "Color Standard";
    case VideoEmulation_t::ColorTextOptimized:
      return "Color Text Optimized";
    case VideoEmulation_t::ColorTvEmulation:
      return "Color TV Emulation";
    case VideoEmulation_t::ColorHalfShift:
      return "Color Half-Shift";
    case VideoEmulation_t::MonochromeAmber:
      return "Monochrome Amber";
    case VideoEmulation_t::MonochromeGreen:
      return "Monochrome Green";
    case VideoEmulation_t::MonochromeWhite:
      return "Monochrome White";
  }
  return "Color Standard";
}

auto video_emulation_from_string(const std::string& str, VideoEmulation_t* out)
    -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "colorstandard") {
    *out = VideoEmulation_t::ColorStandard;
    return true;
  }
  if (norm == "monochromecustom" || norm == "custommonochrome") {
    *out = VideoEmulation_t::MonochromeCustom;
    return true;
  }
  if (norm == "colortextoptimized") {
    *out = VideoEmulation_t::ColorTextOptimized;
    return true;
  }
  if (norm == "colortvemulation") {
    *out = VideoEmulation_t::ColorTvEmulation;
    return true;
  }
  if (norm == "colorhalfshift") {
    *out = VideoEmulation_t::ColorHalfShift;
    return true;
  }
  if (norm == "monochromeamber") {
    *out = VideoEmulation_t::MonochromeAmber;
    return true;
  }
  if (norm == "monochromegreen") {
    *out = VideoEmulation_t::MonochromeGreen;
    return true;
  }
  if (norm == "monochromewhite") {
    *out = VideoEmulation_t::MonochromeWhite;
    return true;
  }
  return false;
}

// --- BasicLineNumbering_t ---
auto basic_line_numbering_to_string(BasicLineNumbering_t numbering) -> const
    char* {
  switch (numbering) {
    case BasicLineNumbering_t::Explicit:
      return "Explicit";
    case BasicLineNumbering_t::Positional:
      return "Positional";
  }
  return "Explicit";
}

auto basic_line_numbering_from_string(const std::string& str,
                                      BasicLineNumbering_t* out) -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "explicit") {
    *out = BasicLineNumbering_t::Explicit;
    return true;
  }
  if (norm == "positional") {
    *out = BasicLineNumbering_t::Positional;
    return true;
  }
  return false;
}

// --- ConfigTuiRenderMode_t ---
auto tui_render_mode_to_string(ConfigTuiRenderMode_t mode) -> const char* {
  switch (mode) {
    case ConfigTuiRenderMode_t::Smart:
      return "Smart";
    case ConfigTuiRenderMode_t::Block:
      return "Block";
  }
  return "Smart";
}

auto tui_render_mode_from_string(const std::string& str,
                                 ConfigTuiRenderMode_t* out) -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "smart") {
    *out = ConfigTuiRenderMode_t::Smart;
    return true;
  }
  if (norm == "block") {
    *out = ConfigTuiRenderMode_t::Block;
    return true;
  }
  return false;
}

// --- SoundcardType_t ---
auto soundcard_type_to_string(SoundcardType_t type) -> const char* {
  switch (type) {
    case SoundcardType_t::None:
      return "None";
    case SoundcardType_t::Mockingboard:
      return "Mockingboard";
    case SoundcardType_t::Phasor:
      return "Phasor";
  }
  return "Mockingboard";
}

auto soundcard_type_from_string(const std::string& str, SoundcardType_t* out)
    -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "mockingboard") {
    *out = SoundcardType_t::Mockingboard;
    return true;
  }
  if (norm == "none") {
    *out = SoundcardType_t::None;
    return true;
  }
  if (norm == "phasor") {
    *out = SoundcardType_t::Phasor;
    return true;
  }
  return false;
}

// --- ConfigKeyboardLayout_t ---
auto keyboard_layout_to_string(ConfigKeyboardLayout_t layout) -> const char* {
  switch (layout) {
    case ConfigKeyboardLayout_t::Auto:
      return "Automatic";
    case ConfigKeyboardLayout_t::US:
      return "US QWERTY";
    case ConfigKeyboardLayout_t::UK:
      return "UK QWERTY";
    case ConfigKeyboardLayout_t::French:
      return "French AZERTY";
    case ConfigKeyboardLayout_t::German:
      return "German QWERTZ";
    case ConfigKeyboardLayout_t::Spanish:
      return "Spanish ES";
  }
  return "Automatic";
}

auto keyboard_layout_from_string(const std::string& str,
                                 ConfigKeyboardLayout_t* out) -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "auto" || norm == "automatic") {
    *out = ConfigKeyboardLayout_t::Auto;
    return true;
  }
  if (norm == "us" || norm == "usqwerty") {
    *out = ConfigKeyboardLayout_t::US;
    return true;
  }
  if (norm == "uk" || norm == "ukqwerty") {
    *out = ConfigKeyboardLayout_t::UK;
    return true;
  }
  if (norm == "french" || norm == "frenchazerty") {
    *out = ConfigKeyboardLayout_t::French;
    return true;
  }
  if (norm == "german" || norm == "germanqwertz") {
    *out = ConfigKeyboardLayout_t::German;
    return true;
  }
  if (norm == "spanish" || norm == "spanishes") {
    *out = ConfigKeyboardLayout_t::Spanish;
    return true;
  }
  return false;
}

// --- KeyboardRocker_t ---
auto keyboard_rocker_to_string(KeyboardRocker_t rocker) -> const char* {
  switch (rocker) {
    case KeyboardRocker_t::US:
      return "US";
    case KeyboardRocker_t::Local:
      return "Local";
  }
  return "US";
}

auto keyboard_rocker_from_string(const std::string& str, KeyboardRocker_t* out)
    -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "us") {
    *out = KeyboardRocker_t::US;
    return true;
  }
  if (norm == "local") {
    *out = KeyboardRocker_t::Local;
    return true;
  }
  return false;
}

// --- KeyMappingMode_t ---
auto key_mapping_mode_to_string(KeyMappingMode_t mode) -> const char* {
  switch (mode) {
    case KeyMappingMode_t::Symbolic:
      return "Symbolic";
    case KeyMappingMode_t::Positional:
      return "Positional";
  }
  return "Symbolic";
}

auto key_mapping_mode_from_string(const std::string& str, KeyMappingMode_t* out)
    -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "symbolic") {
    *out = KeyMappingMode_t::Symbolic;
    return true;
  }
  if (norm == "positional") {
    *out = KeyMappingMode_t::Positional;
    return true;
  }
  return false;
}

// --- ConfigCapsLockMode_t ---
auto caps_lock_mode_to_string(ConfigCapsLockMode_t mode) -> const char* {
  switch (mode) {
    case ConfigCapsLockMode_t::Host:
      return "Host";
    case ConfigCapsLockMode_t::Emulated:
      return "Emulated";
  }
  return "Host";
}

auto caps_lock_mode_from_string(const std::string& str,
                                ConfigCapsLockMode_t* out) -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "host") {
    *out = ConfigCapsLockMode_t::Host;
    return true;
  }
  if (norm == "emulated") {
    *out = ConfigCapsLockMode_t::Emulated;
    return true;
  }
  return false;
}

// --- QuickSaveModifier_t ---
auto quick_save_modifier_to_string(QuickSaveModifier_t mod) -> const char* {
  switch (mod) {
    case QuickSaveModifier_t::Alt:
      return "Alt";
    case QuickSaveModifier_t::Ctrl:
      return "Ctrl";
    case QuickSaveModifier_t::AltCtrl:
      return "AltCtrl";
    case QuickSaveModifier_t::Disabled:
      return "Disabled";
  }
  return "Alt";
}

auto quick_save_modifier_from_string(const std::string& str,
                                     QuickSaveModifier_t* out) -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "alt") {
    *out = QuickSaveModifier_t::Alt;
    return true;
  }
  if (norm == "ctrl" || norm == "control") {
    *out = QuickSaveModifier_t::Ctrl;
    return true;
  }
  if (norm == "altctrl" || norm == "ctrlalt") {
    *out = QuickSaveModifier_t::AltCtrl;
    return true;
  }
  if (norm == "disabled" || norm == "none") {
    *out = QuickSaveModifier_t::Disabled;
    return true;
  }
  return false;
}

// --- JoystickMode_t ---
auto joystick_mode_to_string(JoystickMode_t mode) -> const char* {
  switch (mode) {
    case JoystickMode_t::Disabled:
      return "Disabled";
    case JoystickMode_t::HostGamepad:
      return "Host Gamepad";
    case JoystickMode_t::KeyboardStandard:
      return "Keyboard Standard";
    case JoystickMode_t::KeyboardCentered:
      return "Keyboard Centered";
    case JoystickMode_t::Mouse:
      return "Mouse Joystick";
  }
  return "Disabled";
}

auto joystick_mode_from_string(const std::string& str, JoystickMode_t* out)
    -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm == "disabled" || norm == "none") {
    *out = JoystickMode_t::Disabled;
    return true;
  }
  if (norm == "hostgamepad") {
    *out = JoystickMode_t::HostGamepad;
    return true;
  }
  if (norm == "keyboardstandard") {
    *out = JoystickMode_t::KeyboardStandard;
    return true;
  }
  if (norm == "keyboardcentered") {
    *out = JoystickMode_t::KeyboardCentered;
    return true;
  }
  if (norm == "mousejoystick" || norm == "mouse") {
    *out = JoystickMode_t::Mouse;
    return true;
  }
  return false;
}

// --- PeripheralCardType_t ---
auto peripheral_card_to_string(PeripheralCardType_t card) -> const char* {
  switch (card) {
    case PeripheralCardType_t::Empty:
      return "None";
    case PeripheralCardType_t::ParallelPrinter:
      return "Parallel Printer";
    case PeripheralCardType_t::SuperSerial:
      return "Super Serial Card";
    case PeripheralCardType_t::Mockingboard:
      return "Mockingboard";
    case PeripheralCardType_t::DiskII:
      return "Disk II";
    case PeripheralCardType_t::Harddisk:
      return "Harddisk";
    case PeripheralCardType_t::Mouse:
      return "Mouse";
    case PeripheralCardType_t::Clock:
      return "Clock";
  }
  return "None";
}

auto peripheral_card_from_string(const std::string& str,
                                 PeripheralCardType_t* out) -> bool {
  if (out == nullptr) {
    return false;
  }
  const std::string norm = normalize_identifier(str);
  if (norm.empty() || norm == "none" || norm == "empty") {
    *out = PeripheralCardType_t::Empty;
    return true;
  }
  if (norm == "parallelprinter" || norm == "printer" ||
      norm == "linappleprinter") {
    *out = PeripheralCardType_t::ParallelPrinter;
    return true;
  }
  if (norm == "superserialcard" || norm == "superserial" || norm == "ssc" ||
      norm == "linapplessc") {
    *out = PeripheralCardType_t::SuperSerial;
    return true;
  }
  if (norm == "mockingboard" || norm == "linapplemockingboard") {
    *out = PeripheralCardType_t::Mockingboard;
    return true;
  }
  if (norm == "diskii" || norm == "disk2" || norm == "linapplediskii") {
    *out = PeripheralCardType_t::DiskII;
    return true;
  }
  if (norm == "harddisk" || norm == "linappleharddisk") {
    *out = PeripheralCardType_t::Harddisk;
    return true;
  }
  if (norm == "mouse" || norm == "mouseinterface" || norm == "linapplemouse") {
    *out = PeripheralCardType_t::Mouse;
    return true;
  }
  if (norm == "clock" || norm == "linappleclock") {
    *out = PeripheralCardType_t::Clock;
    return true;
  }
  return false;
}

// --- Defaults ---
auto config_defaults() -> LinAppleConfig_t {
  LinAppleConfig_t config;
  config.core = CoreConfig_t{};
  config.video = VideoConfig_t{};
  config.frontend = FrontendConfig_t{};
  config.audio = AudioConfig_t{};
  config.input = InputConfig_t{};
  config.keyboard = KeyboardConfig_t{};
  config.joystick = JoystickConfig_t{};
  config.slots = SlotsConfig_t{};
  config.raw_doc = nullptr;
  return config;
}

// --- Validation ---
auto config_validate(const LinAppleConfig_t& config,
                     std::vector<ConfigValidationError_t>* out_errors) -> bool {
  bool is_valid = true;

  auto report_error = [&](const std::string& section, const std::string& key,
                          const std::string& msg, bool is_warning = false) {
    if (!is_warning) {
      is_valid = false;
    }
    if (out_errors != nullptr) {
      ConfigValidationError_t err;
      err.section = section;
      err.key = key;
      err.message = msg;
      err.is_warning = is_warning;
      out_errors->push_back(err);
    }
  };

  // Core validation
  if (config.core.emulation_speed < 0.01 ||
      config.core.emulation_speed > 100.0) {
    report_error("Core", "EmulationSpeed",
                 "EmulationSpeed must be between 0.01 and 100.0x");
  }

  // Video validation
  if (config.video.screen_factor < 0.1 || config.video.screen_factor > 10.0) {
    report_error("Video", "ScreenFactor",
                 "ScreenFactor must be between 0.1 and 10.0");
  }
  if (config.video.screen_width < 0 || config.video.screen_height < 0) {
    report_error("Video", "ScreenDimensions",
                 "Screen width and height must not be negative");
  }
  if (!is_valid_hex_color(config.video.monochrome_color)) {
    report_error(
        "Video", "MonochromeColor",
        "MonochromeColor must be a valid hex RGB or RGBA code (e.g. #C0C0C0)");
  }

  // Audio validation
  if (config.audio.speaker_volume < 0 || config.audio.speaker_volume > 100) {
    report_error("Audio", "SpeakerVolume",
                 "SpeakerVolume must be between 0 and 100");
  }
  if (config.audio.mockingboard_volume < 0 ||
      config.audio.mockingboard_volume > 100) {
    report_error("Audio", "MockingboardVolume",
                 "MockingboardVolume must be between 0 and 100");
  }

  // Joystick validation
  if (config.joystick.pdl_x_trim < -128 || config.joystick.pdl_x_trim > 127) {
    report_error("Joystick", "PDLXTrim",
                 "PDLXTrim must be between -128 and 127");
  }
  if (config.joystick.pdl_y_trim < -128 || config.joystick.pdl_y_trim > 127) {
    report_error("Joystick", "PDLYTrim",
                 "PDLYTrim must be between -128 and 127");
  }

  // Slots validation
  if (config.slots.cards[0] != PeripheralCardType_t::Empty) {
    report_error(
        "Slots", "Slot0",
        "Slot 0 is reserved for motherboard memory/firmware and cannot "
        "hold expansion cards");
  }

  return is_valid;
}

// --- TOML Parser & Serializer ---
auto config_from_toml(const TomlDocument_t* doc, LinAppleConfig_t* out_config,
                      std::vector<ConfigValidationError_t>* out_errors)
    -> bool {
  if (doc == nullptr || out_config == nullptr) {
    if (out_errors != nullptr) {
      ConfigValidationError_t err;
      err.section = "Document";
      err.message = "Null document or output pointer";
      out_errors->push_back(err);
    }
    return false;
  }

  *out_config = config_defaults();

  auto get_table_with_fallback =
      [&](const std::string& modern_sec,
          const std::string& legacy_sec) -> const TomlTable_t* {
    const auto* tbl = toml_find_table(doc, modern_sec);
    if (tbl != nullptr) {
      return tbl;
    }
    return toml_find_table(doc, legacy_sec);
  };

  // 1. [Core] (or legacy [Configuration])
  const auto* core_table = get_table_with_fallback("Core", "Configuration");
  if (core_table != nullptr) {
    std::string mach_str = toml_table_get_string(core_table, "Machine", "");
    if (mach_str.empty()) {
      mach_str = toml_table_get_string(core_table, "Computer Emulation", "");
    }
    if (!mach_str.empty()) {
      MachineType_t mach;
      if (machine_type_from_string(mach_str, &mach)) {
        out_config->core.machine = mach;
      } else if (out_errors != nullptr) {
        out_errors->push_back({"Core", "Machine",
                               "Unrecognized machine type: " + mach_str, true});
      }
    }

    if (toml_table_has_key(core_table, "EmulationSpeed")) {
      out_config->core.emulation_speed =
          toml_table_get_double(core_table, "EmulationSpeed", 1.0);
    } else if (toml_table_has_key(core_table, "Emulation Speed")) {
      const int64_t raw_speed =
          toml_table_get_int(core_table, "Emulation Speed", 10);
      out_config->core.emulation_speed = static_cast<double>(raw_speed) / 10.0;
    }

    if (toml_table_has_key(core_table, "EnhanceDiskSpeed")) {
      out_config->core.enhance_disk_speed =
          toml_table_get_bool(core_table, "EnhanceDiskSpeed", true);
    } else if (toml_table_has_key(core_table, "Enhance Disk Speed")) {
      out_config->core.enhance_disk_speed =
          toml_table_get_bool(core_table, "Enhance Disk Speed", true);
    }

    if (toml_table_has_key(core_table, "BootOnStartup")) {
      out_config->core.boot_on_startup =
          toml_table_get_bool(core_table, "BootOnStartup", false);
    } else if (toml_table_has_key(core_table, "Boot at Startup")) {
      out_config->core.boot_on_startup =
          toml_table_get_bool(core_table, "Boot at Startup", false);
    }

    if (toml_table_has_key(core_table, "SaveStateOnExit")) {
      out_config->core.save_state_on_exit =
          toml_table_get_bool(core_table, "SaveStateOnExit", false);
    } else if (toml_table_has_key(core_table, "Save State On Exit")) {
      out_config->core.save_state_on_exit =
          toml_table_get_bool(core_table, "Save State On Exit", false);
    }

    if (toml_table_has_key(core_table, "EnableDebugger")) {
      out_config->core.enable_debugger =
          toml_table_get_bool(core_table, "EnableDebugger", true);
    } else if (toml_table_has_key(core_table, "Disable Debugger")) {
      out_config->core.enable_debugger =
          !toml_table_get_bool(core_table, "Disable Debugger", false);
    }

    out_config->core.basic_sync_file = toml_table_get_string(
        core_table, "BasicLiveSyncFile",
        toml_table_get_string(core_table, "Basic Live Sync File", ""));

    std::string line_mode_str =
        toml_table_get_string(core_table, "BasicLineNumbering", "");
    if (line_mode_str.empty()) {
      line_mode_str =
          toml_table_get_string(core_table, "Basic Line Numbering", "");
    }
    if (!line_mode_str.empty()) {
      BasicLineNumbering_t numbering;
      if (basic_line_numbering_from_string(line_mode_str, &numbering)) {
        out_config->core.basic_line_numbering = numbering;
      }
    }
  }

  // 2. [Video]
  const auto* video_table = get_table_with_fallback("Video", "Configuration");
  if (video_table != nullptr) {
    std::string std_str =
        toml_table_get_string(video_table, "VideoStandard", "");
    if (std_str.empty()) {
      std_str = toml_table_get_string(video_table, "Video Standard", "");
    }
    if (!std_str.empty()) {
      VideoStandard_t standard;
      if (video_standard_from_string(std_str, &standard)) {
        out_config->video.video_standard = standard;
      }
    }

    std::string emu_str =
        toml_table_get_string(video_table, "VideoEmulation", "");
    if (emu_str.empty()) {
      emu_str = toml_table_get_string(video_table, "Video Emulation", "");
    }
    if (!emu_str.empty()) {
      VideoEmulation_t emu;
      if (video_emulation_from_string(emu_str, &emu)) {
        out_config->video.video_emulation = emu;
      }
    }

    out_config->video.monochrome_color = toml_table_get_string(
        video_table, "MonochromeColor",
        toml_table_get_string(video_table, "Monochrome Color", "#C0C0C0"));
  }

  // 3. [Frontend] (with fallback to [Video] and [Configuration])
  const auto* frontend_table = toml_find_table(doc, "Frontend");
  if (frontend_table == nullptr) {
    frontend_table = video_table;
  }
  if (frontend_table != nullptr) {
    if (toml_table_has_key(frontend_table, "ScreenFactor")) {
      out_config->frontend.screen_factor =
          toml_table_get_double(frontend_table, "ScreenFactor", 1.0);
    } else if (toml_table_has_key(frontend_table, "Screen factor")) {
      out_config->frontend.screen_factor =
          toml_table_get_double(frontend_table, "Screen factor", 1.0);
    }

    out_config->frontend.screen_width = static_cast<int>(toml_table_get_int(
        frontend_table, "ScreenWidth",
        toml_table_get_int(frontend_table, "Screen Width", 0)));
    out_config->frontend.screen_height = static_cast<int>(toml_table_get_int(
        frontend_table, "ScreenHeight",
        toml_table_get_int(frontend_table, "Screen Height", 0)));

    out_config->frontend.fullscreen =
        toml_table_get_bool(frontend_table, "Fullscreen", false);

    if (toml_table_has_key(frontend_table, "Multithreaded")) {
      out_config->frontend.multithreaded =
          toml_table_get_bool(frontend_table, "Multithreaded", true);
    } else if (toml_table_has_key(frontend_table, "Singlethreaded")) {
      out_config->frontend.multithreaded =
          !toml_table_get_bool(frontend_table, "Singlethreaded", false);
    }

    if (toml_table_has_key(frontend_table, "EnableHotkeys")) {
      out_config->frontend.enable_hotkeys =
          toml_table_get_bool(frontend_table, "EnableHotkeys", true);
    } else if (toml_table_has_key(frontend_table, "Enable Hotkeys")) {
      out_config->frontend.enable_hotkeys =
          toml_table_get_bool(frontend_table, "Enable Hotkeys", true);
    }

    out_config->frontend.show_leds = toml_table_get_bool(
        frontend_table, "ShowLeds",
        toml_table_get_bool(frontend_table, "Show Leds", true));

    std::string tui_str =
        toml_table_get_string(frontend_table, "RenderMode", "");
    if (tui_str.empty()) {
      tui_str = toml_table_get_string(frontend_table, "TuiRenderMode", "");
    }
    if (tui_str.empty()) {
      tui_str = toml_table_get_string(frontend_table, "TUI Render Mode", "");
    }
    if (!tui_str.empty()) {
      ConfigTuiRenderMode_t tui_mode;
      if (tui_render_mode_from_string(tui_str, &tui_mode)) {
        out_config->frontend.render_mode = tui_mode;
      }
    }

    // Mirror frontend settings to video for backward compatibility
    out_config->video.screen_factor = out_config->frontend.screen_factor;
    out_config->video.screen_width = out_config->frontend.screen_width;
    out_config->video.screen_height = out_config->frontend.screen_height;
    out_config->video.fullscreen = out_config->frontend.fullscreen;
    out_config->video.multithreaded = out_config->frontend.multithreaded;
    out_config->video.show_leds = out_config->frontend.show_leds;
    out_config->video.tui_render_mode = out_config->frontend.render_mode;
  }

  // 4. [Audio]
  const auto* audio_table = get_table_with_fallback("Audio", "Configuration");
  if (audio_table != nullptr) {
    out_config->audio.sound_emulation = toml_table_get_bool(
        audio_table, "SoundEmulation",
        toml_table_get_bool(audio_table, "Sound Emulation", true));

    out_config->audio.speaker_volume = static_cast<int>(toml_table_get_int(
        audio_table, "SpeakerVolume",
        toml_table_get_int(audio_table, "Speaker Volume", 50)));

    out_config->audio.mockingboard_volume = static_cast<int>(toml_table_get_int(
        audio_table, "MockingboardVolume",
        toml_table_get_int(audio_table, "Mockingboard Volume", 50)));

    std::string card_str =
        toml_table_get_string(audio_table, "SoundcardType", "");
    if (card_str.empty()) {
      card_str = toml_table_get_string(audio_table, "Soundcard Type", "");
    }
    if (!card_str.empty()) {
      SoundcardType_t card;
      if (soundcard_type_from_string(card_str, &card)) {
        out_config->audio.soundcard_type = card;
      }
    }
  }

  const auto* mb_table = toml_find_table(doc, "Peripheral.Mockingboard");
  if (mb_table != nullptr) {
    if (toml_table_has_key(mb_table, "Volume")) {
      out_config->audio.mockingboard_volume =
          static_cast<int>(toml_table_get_int(mb_table, "Volume", 50));
    }
    if (toml_table_has_key(mb_table, "Type")) {
      const std::string mb_type = toml_table_get_string(mb_table, "Type", "");
      if (mb_type == "Phasor") {
        out_config->audio.soundcard_type = SoundcardType_t::Phasor;
      } else {
        out_config->audio.soundcard_type = SoundcardType_t::Mockingboard;
      }
    }
  }

  // 5. [Input] (with fallback to [Keyboard], [Joystick], and [Configuration])
  const auto* input_table = toml_find_table(doc, "Input");
  const auto* kbd_table =
      input_table != nullptr
          ? input_table
          : get_table_with_fallback("Keyboard", "Configuration");
  const auto* joy_table =
      input_table != nullptr
          ? input_table
          : get_table_with_fallback("Joystick", "Configuration");

  auto get_str_val = [&](const std::string& k1, const std::string& k2,
                         const std::string& dflt) -> std::string {
    if (input_table != nullptr && toml_table_has_key(input_table, k1)) {
      return toml_table_get_string(input_table, k1, dflt);
    }
    if (input_table != nullptr && !k2.empty() &&
        toml_table_has_key(input_table, k2)) {
      return toml_table_get_string(input_table, k2, dflt);
    }
    if (kbd_table != nullptr && toml_table_has_key(kbd_table, k1)) {
      return toml_table_get_string(kbd_table, k1, dflt);
    }
    if (kbd_table != nullptr && !k2.empty() &&
        toml_table_has_key(kbd_table, k2)) {
      return toml_table_get_string(kbd_table, k2, dflt);
    }
    if (joy_table != nullptr && toml_table_has_key(joy_table, k1)) {
      return toml_table_get_string(joy_table, k1, dflt);
    }
    if (joy_table != nullptr && !k2.empty() &&
        toml_table_has_key(joy_table, k2)) {
      return toml_table_get_string(joy_table, k2, dflt);
    }
    return dflt;
  };

  auto get_bool_val = [&](const std::string& k1, const std::string& k2,
                          bool dflt) -> bool {
    if (input_table != nullptr && toml_table_has_key(input_table, k1)) {
      return toml_table_get_bool(input_table, k1, dflt);
    }
    if (input_table != nullptr && !k2.empty() &&
        toml_table_has_key(input_table, k2)) {
      return toml_table_get_bool(input_table, k2, dflt);
    }
    if (kbd_table != nullptr && toml_table_has_key(kbd_table, k1)) {
      return toml_table_get_bool(kbd_table, k1, dflt);
    }
    if (kbd_table != nullptr && !k2.empty() &&
        toml_table_has_key(kbd_table, k2)) {
      return toml_table_get_bool(kbd_table, k2, dflt);
    }
    if (joy_table != nullptr && toml_table_has_key(joy_table, k1)) {
      return toml_table_get_bool(joy_table, k1, dflt);
    }
    if (joy_table != nullptr && !k2.empty() &&
        toml_table_has_key(joy_table, k2)) {
      return toml_table_get_bool(joy_table, k2, dflt);
    }
    return dflt;
  };

  auto get_int_val = [&](const std::string& k1, const std::string& k2,
                         int64_t dflt) -> int64_t {
    if (input_table != nullptr && toml_table_has_key(input_table, k1)) {
      return toml_table_get_int(input_table, k1, dflt);
    }
    if (input_table != nullptr && !k2.empty() &&
        toml_table_has_key(input_table, k2)) {
      return toml_table_get_int(input_table, k2, dflt);
    }
    if (kbd_table != nullptr && toml_table_has_key(kbd_table, k1)) {
      return toml_table_get_int(kbd_table, k1, dflt);
    }
    if (kbd_table != nullptr && !k2.empty() &&
        toml_table_has_key(kbd_table, k2)) {
      return toml_table_get_int(kbd_table, k2, dflt);
    }
    if (joy_table != nullptr && toml_table_has_key(joy_table, k1)) {
      return toml_table_get_int(joy_table, k1, dflt);
    }
    if (joy_table != nullptr && !k2.empty() &&
        toml_table_has_key(joy_table, k2)) {
      return toml_table_get_int(joy_table, k2, dflt);
    }
    return dflt;
  };

  // Keyboard options
  std::string layout_str = get_str_val("KeyboardType", "Keyboard Type", "");
  if (!layout_str.empty()) {
    ConfigKeyboardLayout_t layout;
    if (keyboard_layout_from_string(layout_str, &layout)) {
      out_config->input.keyboard_type = layout;
    }
  }

  std::string rocker_str =
      get_str_val("KeyboardRockerSwitch", "Keyboard Rocker Switch", "");
  if (!rocker_str.empty()) {
    KeyboardRocker_t rocker;
    if (keyboard_rocker_from_string(rocker_str, &rocker)) {
      out_config->input.keyboard_rocker = rocker;
    }
  }

  std::string map_str = get_str_val("MappingMode", "Mapping Mode", "");
  if (!map_str.empty()) {
    KeyMappingMode_t mode;
    if (key_mapping_mode_from_string(map_str, &mode)) {
      out_config->input.mapping_mode = mode;
    }
  }

  std::string caps_str = get_str_val("CapsLockMode", "Caps Lock Mode", "");
  if (!caps_str.empty()) {
    ConfigCapsLockMode_t mode;
    if (caps_lock_mode_from_string(caps_str, &mode)) {
      out_config->input.caps_lock_mode = mode;
    }
  }

  std::string qs_str =
      get_str_val("QuickSaveModifier", "Quick Save Modifier", "");
  if (!qs_str.empty()) {
    QuickSaveModifier_t mod;
    if (quick_save_modifier_from_string(qs_str, &mod)) {
      out_config->input.quick_save_modifier = mod;
    }
  }

  if (input_table != nullptr &&
      (toml_table_has_key(input_table, "EnableHotkeys") ||
       toml_table_has_key(input_table, "Enable Hotkeys"))) {
    out_config->frontend.enable_hotkeys =
        get_bool_val("EnableHotkeys", "Enable Hotkeys", true);
  }
  out_config->input.enable_hotkeys = out_config->frontend.enable_hotkeys;
  out_config->input.scroll_lock_toggle =
      get_bool_val("ScrollLockToggle", "ScrollLock Toggle", true);

  // Joystick options
  std::string j0_str = get_str_val("Joystick0Mode", "Joystick 0", "");
  if (!j0_str.empty()) {
    JoystickMode_t mode;
    if (joystick_mode_from_string(j0_str, &mode)) {
      out_config->input.joy0_mode = mode;
    }
  }

  std::string j1_str = get_str_val("Joystick1Mode", "Joystick 1", "");
  if (!j1_str.empty()) {
    JoystickMode_t mode;
    if (joystick_mode_from_string(j1_str, &mode)) {
      out_config->input.joy1_mode = mode;
    }
  }

  out_config->input.joy0_index =
      static_cast<int>(get_int_val("Joy0Index", "Joystick 0 Index", 0));
  out_config->input.joy1_index =
      static_cast<int>(get_int_val("Joy1Index", "Joystick 1 Index", 1));

  out_config->input.joy0_button1 =
      static_cast<int>(get_int_val("Joy0Button1", "Joystick 0 Button 1", 0));
  out_config->input.joy0_button2 =
      static_cast<int>(get_int_val("Joy0Button2", "Joystick 0 Button 2", 1));
  out_config->input.joy1_button1 =
      static_cast<int>(get_int_val("Joy1Button1", "Joystick 1 Button 1", 0));
  out_config->input.joy1_button2 =
      static_cast<int>(get_int_val("Joy1Button2", "Joystick 1 Button 2", 1));

  out_config->input.joy0_axis0 =
      static_cast<int>(get_int_val("Joy0Axis0", "Joystick 0 Axis 0", 0));
  out_config->input.joy0_axis1 =
      static_cast<int>(get_int_val("Joy0Axis1", "Joystick 0 Axis 1", 1));
  out_config->input.joy1_axis0 =
      static_cast<int>(get_int_val("Joy1Axis0", "Joystick 1 Axis 0", 0));
  out_config->input.joy1_axis1 =
      static_cast<int>(get_int_val("Joy1Axis1", "Joystick 1 Axis 1", 1));

  out_config->input.pdl_x_trim =
      static_cast<int>(get_int_val("PdlXTrim", "PDL X-Trim", 0));
  out_config->input.pdl_y_trim =
      static_cast<int>(get_int_val("PdlYTrim", "PDL Y-Trim", 0));

  out_config->input.joy_exit_enable =
      get_bool_val("JoyExitEnable", "Joystick Exit Enable", false);
  out_config->input.joy_exit_button0 = static_cast<int>(
      get_int_val("JoyExitButton0", "Joystick Exit Button 0", 8));
  out_config->input.joy_exit_button1 = static_cast<int>(
      get_int_val("JoyExitButton1", "Joystick Exit Button 1", 9));
  out_config->input.mouse_capture =
      get_bool_val("MouseCapture", "Mouse Capture", true);

  // Mirror input to keyboard and joystick for backward compatibility
  out_config->keyboard.layout = out_config->input.keyboard_type;
  out_config->keyboard.rocker_switch = out_config->input.keyboard_rocker;
  out_config->keyboard.mapping_mode = out_config->input.mapping_mode;
  out_config->keyboard.caps_lock_mode = out_config->input.caps_lock_mode;
  out_config->keyboard.quick_save_modifier =
      out_config->input.quick_save_modifier;
  out_config->keyboard.enable_hotkeys = out_config->input.enable_hotkeys;
  out_config->keyboard.scroll_lock_toggle =
      out_config->input.scroll_lock_toggle;

  out_config->joystick.joy0_mode = out_config->input.joy0_mode;
  out_config->joystick.joy1_mode = out_config->input.joy1_mode;
  out_config->joystick.joy0_index = out_config->input.joy0_index;
  out_config->joystick.joy1_index = out_config->input.joy1_index;
  out_config->joystick.joy0_button1 = out_config->input.joy0_button1;
  out_config->joystick.joy0_button2 = out_config->input.joy0_button2;
  out_config->joystick.joy1_button1 = out_config->input.joy1_button1;
  out_config->joystick.joy1_button2 = out_config->input.joy1_button2;
  out_config->joystick.joy0_axis0 = out_config->input.joy0_axis0;
  out_config->joystick.joy0_axis1 = out_config->input.joy0_axis1;
  out_config->joystick.joy1_axis0 = out_config->input.joy1_axis0;
  out_config->joystick.joy1_axis1 = out_config->input.joy1_axis1;
  out_config->joystick.pdl_x_trim = out_config->input.pdl_x_trim;
  out_config->joystick.pdl_y_trim = out_config->input.pdl_y_trim;
  out_config->joystick.joy_exit_enable = out_config->input.joy_exit_enable;
  out_config->joystick.joy_exit_button0 = out_config->input.joy_exit_button0;
  out_config->joystick.joy_exit_button1 = out_config->input.joy_exit_button1;
  out_config->joystick.mouse_capture = out_config->input.mouse_capture;

  // 6. [Slots]
  const auto* slots_table = toml_find_table(doc, "Slots");
  if (slots_table != nullptr) {
    for (size_t s = 0; s < config_slot_count; ++s) {
      const std::string key1 = "Slot" + std::to_string(s);
      const std::string key2 = "Slot " + std::to_string(s);
      std::string card_str = toml_table_get_string(slots_table, key1, "");
      if (card_str.empty()) {
        card_str = toml_table_get_string(slots_table, key2, "");
      }
      if (!card_str.empty()) {
        PeripheralCardType_t card;
        if (peripheral_card_from_string(card_str, &card)) {
          out_config->slots.cards[s] = card;
        } else if (out_errors != nullptr) {
          out_errors->push_back(ConfigValidationError_t(
              "Slots", key1, "Unrecognized peripheral card: " + card_str,
              true));
        }
      }
    }
  }

  // Save copy of raw document for peripheral passthrough sections
  const std::string serialized = toml_document_serialize(doc);
  out_config->raw_doc = toml_document_parse(serialized, nullptr);

  return config_validate(*out_config, out_errors);
}

auto peripheral_format_option_comment(const PeripheralConfigOption_t& opt)
    -> std::string {
  std::ostringstream ss;
  if (opt.description != nullptr && std::strlen(opt.description) > 0) {
    ss << opt.description;
    if (ss.str().back() != ':') {
      ss << ":";
    }
    ss << "\n";
  }
  if (opt.type == peripheral_config_bool) {
    ss << "  Allowed: true, false\n";
  } else if (opt.allowed_values != nullptr) {
    ss << "  Allowed: ";
    for (size_t i = 0; opt.allowed_values[i] != nullptr; ++i) {
      if (i > 0) {
        ss << ", ";
      }
      ss << opt.allowed_values[i];
    }
    ss << "\n";
  } else if (opt.type == peripheral_config_int && opt.max_int > opt.min_int) {
    ss << "  Range: " << opt.min_int << " to " << opt.max_int << "\n";
  }
  if (opt.default_value != nullptr && std::strlen(opt.default_value) > 0) {
    ss << "  Default: " << opt.default_value;
  }
  return ss.str();
}

extern auto peripheral_get_builtin_registry() -> std::vector<Peripheral_t*>&;

auto config_get_allowed_cards_list() -> std::string {
  std::vector<std::string> cards;
  for (const auto* p : peripheral_get_builtin_registry()) {
    if (p != nullptr &&
        (p->compatible_slots & PERIPHERAL_MASK_EXPANSION) != 0 &&
        p->name != nullptr) {
      cards.emplace_back(p->name);
    }
  }
  std::string result;
  for (size_t i = 0; i < cards.size(); ++i) {
    if (i > 0) {
      result += ", ";
    }
    result += cards[i];
  }
  if (!result.empty()) {
    result += ", None";
  } else {
    result = "None";
  }
  return result;
}

auto peripheral_populate_toml_table(
    TomlDocument_t* doc, const std::string& section_name,
    const char* peripheral_id,
    const std::map<std::string, std::string>& override_values) -> void {
  if (doc == nullptr || peripheral_id == nullptr) {
    return;
  }
  const auto* p = peripheral_find_builtin(peripheral_id);
  if (p != nullptr && p->description != nullptr &&
      std::strlen(p->description) > 0) {
    toml_document_set_section_comment(doc, section_name, p->description);
  }
  const auto* schema = peripheral_get_config_schema_by_id(peripheral_id);
  if (schema == nullptr) {
    return;
  }
  auto* tbl = toml_get_or_create_table(doc, section_name);
  if (tbl == nullptr) {
    return;
  }
  for (size_t i = 0; i < schema->option_count; ++i) {
    const auto& opt = schema->options[i];
    if (opt.name == nullptr) {
      continue;
    }
    std::string val_str =
        (opt.default_value != nullptr) ? opt.default_value : "";
    const auto oit = override_values.find(opt.name);
    if (oit != override_values.end()) {
      val_str = oit->second;
    }
    const std::string pre_comm = peripheral_format_option_comment(opt);
    if (opt.type == peripheral_config_bool) {
      const bool b = (val_str == "true" || val_str == "1" || val_str == "yes" ||
                      val_str == "on");
      toml_table_set_bool(tbl, opt.name, b, "", pre_comm);
    } else if (opt.type == peripheral_config_int) {
      int64_t v = 0;
      if (!val_str.empty()) {
        char* end_ptr = nullptr;
        v = std::strtoll(val_str.c_str(), &end_ptr, 10);
      }
      toml_table_set_int(tbl, opt.name, v, "", pre_comm);
    } else {
      toml_table_set_string(tbl, opt.name, val_str, "", pre_comm);
    }
  }
  for (const auto& kv : override_values) {
    bool in_schema = false;
    for (size_t i = 0; i < schema->option_count; ++i) {
      if (schema->options[i].name != nullptr &&
          kv.first == schema->options[i].name) {
        in_schema = true;
        break;
      }
    }
    if (!in_schema) {
      toml_table_set_string(tbl, kv.first, kv.second);
    }
  }
}

auto config_to_toml(const LinAppleConfig_t& config)
    -> std::unique_ptr<TomlDocument_t> {
  auto doc = toml_document_create();

  // 1. [Core]
  toml_document_set_section_comment(
      doc.get(), "Core",
      "LinApple Core Emulation Configuration\n"
      "Apple II system model, CPU execution speed, and startup options");
  auto* core = toml_get_or_create_table(doc.get(), "Core");
  toml_table_set_string(core, "Machine",
                        machine_type_to_string(config.core.machine), "",
                        "Apple II machine model:\n"
                        "  Allowed: Apple ][, Apple ][+, Apple //e, Apple //e "
                        "Enhanced, Apple //e "
                        "(Japanese)\n"
                        "  Default: Apple //e Enhanced");
  toml_table_set_double(
      core, "EmulationSpeed", config.core.emulation_speed, "",
      "Emulation speed multiplier (1.0 = 1.023 MHz, 0.1 to 100.0):\n"
      "  Default: 1.0");
  toml_table_set_bool(core, "EnhanceDiskSpeed", config.core.enhance_disk_speed,
                      "",
                      "Accelerate CPU execution during disk accesses:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");
  toml_table_set_bool(core, "BootOnStartup", config.core.boot_on_startup, "",
                      "Automatically boot floppy in Slot 6 on startup:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");
  toml_table_set_bool(core, "SaveStateOnExit", config.core.save_state_on_exit,
                      "",
                      "Restore and save emulator state on exit:\n"
                      "  Allowed: true, false\n"
                      "  Default: false");
  toml_table_set_bool(core, "EnableDebugger", config.core.enable_debugger, "",
                      "Integrated 6502 assembly debugger:\n"
                      "  Allowed: true, false\n"
                      "  Default: false");
  if (!config.core.basic_sync_file.empty()) {
    toml_table_set_string(
        core, "BasicLiveSyncFile", config.core.basic_sync_file, "",
        "Bidirectional Applesoft BASIC live sync path (leave empty to "
        "disable):");
  }
  toml_table_set_string(
      core, "BasicLineNumbering",
      basic_line_numbering_to_string(config.core.basic_line_numbering), "",
      "Applesoft BASIC live sync line numbering:\n"
      "  Allowed: Explicit, Positional\n"
      "  Default: Positional");

  // 2. [Video]
  toml_document_set_section_comment(
      doc.get(), "Video",
      "Video Display & Rendering Options\n"
      "Video standards, color palettes, and monitor emulation");
  auto* video = toml_get_or_create_table(doc.get(), "Video");
  toml_table_set_string(video, "VideoStandard",
                        video_standard_to_string(config.video.video_standard),
                        "",
                        "Video timing standard:\n"
                        "  Allowed: NTSC, PAL\n"
                        "  Default: NTSC");
  toml_table_set_string(
      video, "VideoEmulation",
      video_emulation_to_string(config.video.video_emulation), "",
      "Display emulation color palette mode:\n"
      "  Allowed: Color Standard, Color Text Optimized, Color TV Emulation, "
      "Color Half-Shift, Monochrome Custom, Monochrome Amber, Monochrome "
      "Green, "
      "Monochrome White\n"
      "  Default: Color Standard");
  toml_table_set_string(video, "MonochromeColor", config.video.monochrome_color,
                        "",
                        "Custom monochrome RGB hex code:\n"
                        "  Default: #C0C0C0");

  // 3. [Frontend]
  FrontendConfig_t frontend_cfg = config.frontend;
  if (config.video.screen_factor != 1.0 && frontend_cfg.screen_factor == 1.0) {
    frontend_cfg.screen_factor = config.video.screen_factor;
  }
  if (config.video.screen_width != 0 && frontend_cfg.screen_width == 0) {
    frontend_cfg.screen_width = config.video.screen_width;
  }
  if (config.video.screen_height != 0 && frontend_cfg.screen_height == 0) {
    frontend_cfg.screen_height = config.video.screen_height;
  }
  if (config.video.fullscreen && !frontend_cfg.fullscreen) {
    frontend_cfg.fullscreen = config.video.fullscreen;
  }
  if (!config.video.multithreaded && frontend_cfg.multithreaded) {
    frontend_cfg.multithreaded = config.video.multithreaded;
  }
  if (!config.video.show_leds && frontend_cfg.show_leds) {
    frontend_cfg.show_leds = config.video.show_leds;
  }
  if (config.video.tui_render_mode != ConfigTuiRenderMode_t::Smart &&
      frontend_cfg.render_mode == ConfigTuiRenderMode_t::Smart) {
    frontend_cfg.render_mode = config.video.tui_render_mode;
  }

  toml_document_set_section_comment(
      doc.get(), "Frontend",
      "Host Presentation & Frontend Configuration\n"
      "Manages host window sizing, scaling, rendering pipelines, and display "
      "overlays\nacross graphical (SDL) and terminal (TUI) frontends.");
  auto* frontend = toml_get_or_create_table(doc.get(), "Frontend");
  toml_table_set_double(frontend, "ScreenFactor", frontend_cfg.screen_factor,
                        "",
                        "Window scaling factor (1.0 to 10.0):\n"
                        "  Default: 2.0");
  if (frontend_cfg.screen_width > 0 && frontend_cfg.screen_height > 0) {
    toml_table_set_int(frontend, "ScreenWidth", frontend_cfg.screen_width, "",
                       "Custom window width in pixels:");
    toml_table_set_int(frontend, "ScreenHeight", frontend_cfg.screen_height, "",
                       "Custom window height in pixels:");
  }
  toml_table_set_bool(frontend, "Fullscreen", frontend_cfg.fullscreen, "",
                      "Launch emulator in fullscreen mode:\n"
                      "  Allowed: true, false\n"
                      "  Default: false");
  toml_table_set_bool(frontend, "Multithreaded", frontend_cfg.multithreaded, "",
                      "Multithreaded video rendering:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");
  toml_table_set_bool(frontend, "ShowLeds", frontend_cfg.show_leds, "",
                      "On-screen disk activity LED indicator overlay:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");
  toml_table_set_string(frontend, "RenderMode",
                        tui_render_mode_to_string(frontend_cfg.render_mode), "",
                        "Terminal UI (TUI) rendering mode:\n"
                        "  Allowed: Smart, Block\n"
                        "  Default: Smart");

  if (!config.input.enable_hotkeys && frontend_cfg.enable_hotkeys) {
    frontend_cfg.enable_hotkeys = config.input.enable_hotkeys;
  }
  if (!config.keyboard.enable_hotkeys && frontend_cfg.enable_hotkeys) {
    frontend_cfg.enable_hotkeys = config.keyboard.enable_hotkeys;
  }
  toml_table_set_bool(frontend, "EnableHotkeys", frontend_cfg.enable_hotkeys,
                      "",
                      "Enable F1-F12 emulator shortcut keys:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");

  // 4. [Audio]
  toml_document_set_section_comment(
      doc.get(), "Audio",
      "Audio & Sound Hardware Options\n"
      "Apple II built-in speaker and audio output");
  auto* audio = toml_get_or_create_table(doc.get(), "Audio");
  toml_table_set_bool(audio, "SoundEmulation", config.audio.sound_emulation, "",
                      "Enable host audio output:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");
  toml_table_set_int(audio, "SpeakerVolume", config.audio.speaker_volume, "",
                     "Apple II built-in speaker volume (0-100):\n"
                     "  Range: 0 to 100\n"
                     "  Default: 50");

  // 5. [Input]
  InputConfig_t input_cfg = config.input;
  if (config.keyboard.layout != ConfigKeyboardLayout_t::Auto &&
      input_cfg.keyboard_type == ConfigKeyboardLayout_t::Auto) {
    input_cfg.keyboard_type = config.keyboard.layout;
  }
  if (config.keyboard.rocker_switch != KeyboardRocker_t::US &&
      input_cfg.keyboard_rocker == KeyboardRocker_t::US) {
    input_cfg.keyboard_rocker = config.keyboard.rocker_switch;
  }
  if (config.keyboard.mapping_mode != KeyMappingMode_t::Symbolic &&
      input_cfg.mapping_mode == KeyMappingMode_t::Symbolic) {
    input_cfg.mapping_mode = config.keyboard.mapping_mode;
  }
  if (config.keyboard.caps_lock_mode != ConfigCapsLockMode_t::Host &&
      input_cfg.caps_lock_mode == ConfigCapsLockMode_t::Host) {
    input_cfg.caps_lock_mode = config.keyboard.caps_lock_mode;
  }
  if (config.keyboard.quick_save_modifier != QuickSaveModifier_t::Alt &&
      input_cfg.quick_save_modifier == QuickSaveModifier_t::Alt) {
    input_cfg.quick_save_modifier = config.keyboard.quick_save_modifier;
  }
  if (!config.keyboard.enable_hotkeys && input_cfg.enable_hotkeys) {
    input_cfg.enable_hotkeys = config.keyboard.enable_hotkeys;
  }
  if (!config.keyboard.scroll_lock_toggle && input_cfg.scroll_lock_toggle) {
    input_cfg.scroll_lock_toggle = config.keyboard.scroll_lock_toggle;
  }
  if (config.joystick.joy0_mode != JoystickMode_t::KeyboardStandard &&
      input_cfg.joy0_mode == JoystickMode_t::KeyboardStandard) {
    input_cfg.joy0_mode = config.joystick.joy0_mode;
  }
  if (config.joystick.joy1_mode != JoystickMode_t::Disabled &&
      input_cfg.joy1_mode == JoystickMode_t::Disabled) {
    input_cfg.joy1_mode = config.joystick.joy1_mode;
  }
  if (config.joystick.joy0_index != 0) {
    input_cfg.joy0_index = config.joystick.joy0_index;
  }
  if (config.joystick.joy1_index != 1) {
    input_cfg.joy1_index = config.joystick.joy1_index;
  }
  if (config.joystick.joy0_button1 != 0) {
    input_cfg.joy0_button1 = config.joystick.joy0_button1;
  }
  if (config.joystick.joy0_button2 != 1) {
    input_cfg.joy0_button2 = config.joystick.joy0_button2;
  }
  if (config.joystick.joy1_button1 != 0) {
    input_cfg.joy1_button1 = config.joystick.joy1_button1;
  }
  if (config.joystick.joy1_button2 != 1) {
    input_cfg.joy1_button2 = config.joystick.joy1_button2;
  }
  if (config.joystick.joy0_axis0 != 0) {
    input_cfg.joy0_axis0 = config.joystick.joy0_axis0;
  }
  if (config.joystick.joy0_axis1 != 1) {
    input_cfg.joy0_axis1 = config.joystick.joy0_axis1;
  }
  if (config.joystick.joy1_axis0 != 0) {
    input_cfg.joy1_axis0 = config.joystick.joy1_axis0;
  }
  if (config.joystick.joy1_axis1 != 1) {
    input_cfg.joy1_axis1 = config.joystick.joy1_axis1;
  }
  if (config.joystick.pdl_x_trim != 0) {
    input_cfg.pdl_x_trim = config.joystick.pdl_x_trim;
  }
  if (config.joystick.pdl_y_trim != 0) {
    input_cfg.pdl_y_trim = config.joystick.pdl_y_trim;
  }
  if (config.joystick.joy_exit_enable) {
    input_cfg.joy_exit_enable = config.joystick.joy_exit_enable;
  }
  if (config.joystick.joy_exit_button0 != 8) {
    input_cfg.joy_exit_button0 = config.joystick.joy_exit_button0;
  }
  if (config.joystick.joy_exit_button1 != 9) {
    input_cfg.joy_exit_button1 = config.joystick.joy_exit_button1;
  }
  if (!config.joystick.mouse_capture) {
    input_cfg.mouse_capture = config.joystick.mouse_capture;
  }

  toml_document_set_section_comment(
      doc.get(), "Input",
      "Input Device Configuration\n"
      "Keyboard layout, mapping modes, and joystick calibration");
  auto* input = toml_get_or_create_table(doc.get(), "Input");
  toml_table_set_string(
      input, "KeyboardType", keyboard_layout_to_string(input_cfg.keyboard_type),
      "",
      "Keyboard layout (Auto selects layout based on host system settings):\n"
      "  Allowed: Auto, US, UK, French, German, Spanish\n"
      "  Default: Auto");
  toml_table_set_string(input, "KeyboardRockerSwitch",
                        keyboard_rocker_to_string(input_cfg.keyboard_rocker),
                        "",
                        "Keyboard rocker switch:\n"
                        "  Allowed: US, Local\n"
                        "  Default: US");
  toml_table_set_string(
      input, "MappingMode", key_mapping_mode_to_string(input_cfg.mapping_mode),
      "",
      "Key mapping mode (Symbolic translates host characters to Apple II;\n"
      "Positional maps physical host key locations to Apple II layout for "
      "games):\n"
      "  Allowed: Symbolic, Positional\n"
      "  Default: Symbolic");
  toml_table_set_string(input, "CapsLockMode",
                        caps_lock_mode_to_string(input_cfg.caps_lock_mode), "",
                        "Caps lock handling mode:\n"
                        "  Allowed: Host, Emulated\n"
                        "  Default: Host");
  toml_table_set_string(
      input, "QuickSaveModifier",
      quick_save_modifier_to_string(input_cfg.quick_save_modifier), "",
      "Keyboard shortcut modifier for quick-save:\n"
      "  Allowed: Alt, Ctrl, AltCtrl, Disabled\n"
      "  Default: Alt");
  toml_table_set_bool(input, "ScrollLockToggle", input_cfg.scroll_lock_toggle,
                      "",
                      "ScrollLock toggles unthrottled emulation speed:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");

  toml_table_set_string(input, "Joystick0Mode",
                        joystick_mode_to_string(input_cfg.joy0_mode), "",
                        "Joystick 0 emulation mode:\n"
                        "  Allowed: Disabled, Host Gamepad, Keyboard Standard, "
                        "Keyboard Centered, "
                        "Mouse Joystick\n"
                        "  Default: Keyboard Standard");
  toml_table_set_string(input, "Joystick1Mode",
                        joystick_mode_to_string(input_cfg.joy1_mode), "",
                        "Joystick 1 emulation mode:\n"
                        "  Allowed: Disabled, Host Gamepad, Keyboard Standard, "
                        "Keyboard Centered, "
                        "Mouse Joystick\n"
                        "  Default: Disabled");
  toml_table_set_int(input, "Joy0Index", input_cfg.joy0_index, "",
                     "Host gamepad device index for Joystick 0:\n"
                     "  Default: 0");
  toml_table_set_int(input, "Joy1Index", input_cfg.joy1_index, "",
                     "Host gamepad device index for Joystick 1:\n"
                     "  Default: 1");
  toml_table_set_int(
      input, "Joy0Button1", input_cfg.joy0_button1, "",
      "Host gamepad button mapping for Apple II Button 0 (PB0):\n"
      "  Default: 0");
  toml_table_set_int(
      input, "Joy0Button2", input_cfg.joy0_button2, "",
      "Host gamepad button mapping for Apple II Button 1 (PB1):\n"
      "  Default: 1");
  toml_table_set_int(
      input, "Joy1Button1", input_cfg.joy1_button1, "",
      "Host gamepad button mapping for Joystick 1 Apple II Button 0 (PB0):\n"
      "  Default: 0");
  toml_table_set_int(
      input, "Joy1Button2", input_cfg.joy1_button2, "",
      "Host gamepad button mapping for Joystick 1 Apple II Button 1 (PB1):\n"
      "  Default: 1");
  toml_table_set_int(input, "Joy0Axis0", input_cfg.joy0_axis0, "",
                     "Host gamepad axis mapping for Joystick 0 X:\n"
                     "  Default: 0");
  toml_table_set_int(input, "Joy0Axis1", input_cfg.joy0_axis1, "",
                     "Host gamepad axis mapping for Joystick 0 Y:\n"
                     "  Default: 1");
  toml_table_set_int(input, "Joy1Axis0", input_cfg.joy1_axis0, "",
                     "Host gamepad axis mapping for Joystick 1 X:\n"
                     "  Default: 0");
  toml_table_set_int(input, "Joy1Axis1", input_cfg.joy1_axis1, "",
                     "Host gamepad axis mapping for Joystick 1 Y:\n"
                     "  Default: 1");
  toml_table_set_int(input, "PdlXTrim", input_cfg.pdl_x_trim, "",
                     "Paddle X trim offset (-128 to 127):\n"
                     "  Range: -128 to 127\n"
                     "  Default: 0");
  toml_table_set_int(input, "PdlYTrim", input_cfg.pdl_y_trim, "",
                     "Paddle Y trim offset (-128 to 127):\n"
                     "  Range: -128 to 127\n"
                     "  Default: 0");
  toml_table_set_bool(input, "JoyExitEnable", input_cfg.joy_exit_enable, "",
                      "Exit emulator on simultaneous gamepad button chord:\n"
                      "  Allowed: true, false\n"
                      "  Default: false");
  toml_table_set_int(input, "JoyExitButton0", input_cfg.joy_exit_button0, "",
                     "Exit chord first button index:\n"
                     "  Default: 8");
  toml_table_set_int(input, "JoyExitButton1", input_cfg.joy_exit_button1, "",
                     "Exit chord second button index:\n"
                     "  Default: 9");
  toml_table_set_bool(input, "MouseCapture", input_cfg.mouse_capture, "",
                      "Capture host mouse pointer on window click:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");

  // 6. [Slots]
  toml_document_set_section_comment(
      doc.get(), "Slots",
      "Expansion Slot Card Assignments (Slots 1-7)\n"
      "Run 'linapple --list-hardware' to view all available peripherals.");
  auto* slots = toml_get_or_create_table(doc.get(), "Slots");
  for (size_t s = 1; s < config_slot_count; ++s) {
    const std::string key = "Slot" + std::to_string(s);
    toml_table_set_string(slots, key,
                          peripheral_card_to_string(config.slots.cards[s]));
  }

  // 7. Generate peripheral sections for active slot cards using their schemas
  for (size_t s = 1; s < config_slot_count; ++s) {
    const auto card = config.slots.cards[s];
    if (card == PeripheralCardType_t::Empty) {
      continue;
    }
    const char* card_id = config_card_type_to_id(card);
    if (card_id == nullptr) {
      continue;
    }
    std::string sec_name = "Peripheral.";
    if (card == PeripheralCardType_t::ParallelPrinter) {
      sec_name += "ParallelPrinter";
    } else {
      const char* csec = config_card_type_to_section(card);
      if (csec != nullptr) {
        sec_name += csec;
      } else {
        sec_name += peripheral_card_to_string(card);
      }
    }
    if (toml_find_table(doc.get(), sec_name) == nullptr) {
      std::map<std::string, std::string> overrides;
      if (card == PeripheralCardType_t::Mockingboard) {
        overrides["Volume"] = std::to_string(config.audio.mockingboard_volume);
        if (config.audio.soundcard_type == SoundcardType_t::Phasor) {
          overrides["Type"] = "Phasor";
        } else {
          overrides["Type"] = "Mockingboard";
        }
      } else if (card == PeripheralCardType_t::DiskII) {
        std::string master = Path::find_data_file("Master.dsk");
        if (master.empty()) {
          master = "Master.dsk";
        }
        overrides["Drive1"] = master;
      }
      peripheral_populate_toml_table(doc.get(), sec_name, card_id, overrides);
    }
  }

  // 8. Copy over any custom or peripheral tables from raw_doc
  if (config.raw_doc != nullptr) {
    for (const auto& sec_name : config.raw_doc->section_order) {
      if (sec_name == "Core" || sec_name == "Configuration" ||
          sec_name == "Video" || sec_name == "Frontend" ||
          sec_name == "Audio" || sec_name == "Input" ||
          sec_name == "Keyboard" || sec_name == "Joystick" ||
          sec_name == "Slots") {
        continue;
      }
      const auto* src_table = toml_find_table(config.raw_doc.get(), sec_name);
      if (src_table != nullptr) {
        auto* dst_table = toml_get_or_create_table(doc.get(), sec_name);
        if (dst_table != nullptr) {
          *dst_table = *src_table;
        }
      }
      const auto sc_it = config.raw_doc->section_comments.find(sec_name);
      if (sc_it != config.raw_doc->section_comments.end()) {
        toml_document_set_section_comment(doc.get(), sec_name, sc_it->second);
      }
    }
  }

  return doc;
}
