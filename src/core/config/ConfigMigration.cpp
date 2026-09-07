// SPDX-License-Identifier: GPL-2.0-only
#include "core/config/ConfigMigration.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/Util_Path.h"
#include "core/config/ConfigDiscovery.h"
#include "core/config/ConfigSchema.h"
#include "core/config/Toml.h"

namespace {

auto trim(const std::string& str) -> std::string {
  size_t first = 0;
  while (first < str.size() &&
         std::isspace(static_cast<unsigned char>(str[first])) != 0) {
    ++first;
  }
  if (first == str.size()) {
    return "";
  }
  size_t last = str.size() - 1;
  while (last > first &&
         std::isspace(static_cast<unsigned char>(str[last])) != 0) {
    --last;
  }
  return str.substr(first, last - first + 1);
}

auto unquote(const std::string& str) -> std::string {
  if (str.size() >= 2 && ((str.front() == '"' && str.back() == '"') ||
                          (str.front() == '\'' && str.back() == '\''))) {
    return str.substr(1, str.size() - 2);
  }
  return str;
}

auto to_lower(std::string str) -> std::string {
  for (char& ch : str) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return str;
}

struct CaseInsensitiveCompare_t {
  auto operator()(const std::string& lhs, const std::string& rhs) const
      -> bool {
    return to_lower(lhs) < to_lower(rhs);
  }
};

using IniSection_t =
    std::map<std::string, std::string, CaseInsensitiveCompare_t>;
using IniFile_t = std::map<std::string, IniSection_t, CaseInsensitiveCompare_t>;

auto parse_ini(const std::string& content,
               std::vector<std::string>& section_order) -> IniFile_t {
  IniFile_t ini;
  std::istringstream stream(content);
  std::string line;
  std::string current_section = "Configuration";

  section_order.push_back(current_section);

  while (std::getline(stream, line)) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
      continue;
    }

    if (trimmed.front() == '[' && trimmed.back() == ']') {
      current_section = trim(trimmed.substr(1, trimmed.size() - 2));
      if (std::find(section_order.begin(), section_order.end(),
                    current_section) == section_order.end()) {
        section_order.push_back(current_section);
      }
      continue;
    }

    const size_t eq_pos = trimmed.find('=');
    if (eq_pos != std::string::npos) {
      const std::string key = trim(trimmed.substr(0, eq_pos));
      const std::string val = unquote(trim(trimmed.substr(eq_pos + 1)));
      ini[current_section][key] = val;
    }
  }

  return ini;
}

auto get_val(const IniFile_t& ini, const std::string& section,
             const std::string& key, const std::string& def_val = "")
    -> std::string {
  const auto sit = ini.find(section);
  if (sit == ini.end()) {
    return def_val;
  }
  const auto kit = sit->second.find(key);
  if (kit == sit->second.end()) {
    return def_val;
  }
  return kit->second;
}

auto get_val_fallback(const IniFile_t& ini, const std::string& primary_section,
                      const std::string& key, const std::string& def_val = "")
    -> std::string {
  const std::string primary = get_val(ini, primary_section, key);
  if (!primary.empty()) {
    return primary;
  }
  for (const auto& sec_pair : ini) {
    if (sec_pair.first == primary_section) {
      continue;
    }
    const auto kit = sec_pair.second.find(key);
    if (kit != sec_pair.second.end() && !kit->second.empty()) {
      return kit->second;
    }
  }
  return def_val;
}

auto has_key(const IniFile_t& ini, const std::string& section,
             const std::string& key) -> bool {
  const auto sit = ini.find(section);
  if (sit == ini.end()) {
    return false;
  }
  return sit->second.find(key) != sit->second.end();
}

auto get_int(const IniFile_t& ini, const std::string& section,
             const std::string& key, int64_t def_val) -> int64_t {
  const std::string val = get_val(ini, section, key);
  if (val.empty()) {
    return def_val;
  }
  try {
    return std::stoll(val, nullptr, 0);
  } catch (...) {
    return def_val;
  }
}

auto get_double(const IniFile_t& ini, const std::string& section,
                const std::string& key, double def_val) -> double {
  const std::string val = get_val(ini, section, key);
  if (val.empty()) {
    return def_val;
  }
  try {
    return std::stod(val);
  } catch (...) {
    return def_val;
  }
}

auto get_bool(const IniFile_t& ini, const std::string& section,
              const std::string& key, bool def_val) -> bool {
  const std::string val = to_lower(get_val(ini, section, key));
  if (val.empty()) {
    return def_val;
  }
  if (val == "1" || val == "true" || val == "yes" || val == "on") {
    return true;
  }
  if (val == "0" || val == "false" || val == "no" || val == "off") {
    return false;
  }
  return def_val;
}

auto scale_volume(int64_t raw_vol) -> int64_t {
  if (raw_vol <= 0) {
    return 0;
  }
  if (raw_vol > 31) {
    return raw_vol > 100 ? 100 : raw_vol;
  }
  // Map 0..31 to 0..100 with rounding
  return (raw_vol * 100 + 15) / 31;
}

auto map_machine_type(const std::string& val) -> std::string {
  if (val.empty()) {
    return "Apple //e Enhanced";
  }
  if (val == "0") return "Apple ][";
  if (val == "1") return "Apple ][+";
  if (val == "2") return "Apple //e";
  if (val == "3") return "Apple //e Enhanced";
  if (val == "4") return "Apple //e (Japanese)";

  MachineType_t mach;
  if (machine_type_from_string(val, &mach)) {
    return machine_type_to_string(mach);
  }
  return val;
}

auto map_video_standard(const std::string& val) -> std::string {
  if (val == "1" || to_lower(val) == "pal") {
    return "PAL";
  }
  return "NTSC";
}

auto map_video_emulation(const std::string& val) -> std::string {
  if (val == "0") return "Custom Monochrome";
  if (val == "1") return "Color Standard";
  if (val == "2") return "Color Text Optimized";
  if (val == "3") return "Color TV Emulation";
  if (val == "4") return "Color Half-Shift";
  if (val == "5") return "Monochrome Amber";
  if (val == "6") return "Monochrome Green";
  if (val == "7") return "Monochrome White";

  VideoEmulation_t mode;
  if (video_emulation_from_string(val, &mode)) {
    return video_emulation_to_string(mode);
  }
  return "Color Standard";
}

auto map_soundcard_type(const std::string& val) -> std::string {
  if (val == "1") return "None";
  if (val == "2") return "Mockingboard";
  if (val == "3") return "Phasor";

  SoundcardType_t card;
  if (soundcard_type_from_string(val, &card)) {
    return soundcard_type_to_string(card);
  }
  return "Mockingboard";
}

auto map_keyboard_layout(const std::string& val) -> std::string {
  if (val == "0") return "Auto";
  if (val == "1") return "US";
  if (val == "2") return "UK";
  if (val == "3") return "French";
  if (val == "4") return "German";
  if (val == "5") return "Spanish";

  ConfigKeyboardLayout_t layout;
  if (keyboard_layout_from_string(val, &layout)) {
    return keyboard_layout_to_string(layout);
  }
  return "Auto";
}

auto map_keyboard_rocker(const std::string& val) -> std::string {
  if (val == "1" || to_lower(val) == "local") {
    return "Local";
  }
  return "US";
}

auto map_key_mapping_mode(const std::string& val) -> std::string {
  if (val == "1" || to_lower(val) == "positional") {
    return "Positional";
  }
  return "Symbolic";
}

auto map_caps_lock_mode(const std::string& val) -> std::string {
  if (val == "1" || to_lower(val) == "emulated") {
    return "Emulated";
  }
  return "Host";
}

auto map_joystick_mode(const std::string& val) -> std::string {
  if (val == "0") return "Disabled";
  if (val == "1") return "Host Gamepad";
  if (val == "2") return "Keyboard Standard";
  if (val == "3") return "Keyboard Centered";
  if (val == "4") return "Mouse Joystick";

  JoystickMode_t mode;
  if (joystick_mode_from_string(val, &mode)) {
    return joystick_mode_to_string(mode);
  }
  return "Disabled";
}

auto map_peripheral_card(const std::string& val) -> std::string {
  PeripheralCardType_t card;
  if (peripheral_card_from_string(val, &card)) {
    return peripheral_card_to_string(card);
  }
  return val.empty() ? "None" : val;
}

}  // namespace

auto config_migrate_legacy_to_toml(const std::string& ini_content,
                                   std::string* out_error)
    -> std::unique_ptr<TomlDocument_t> {
  std::vector<std::string> section_order;
  IniFile_t ini = parse_ini(ini_content, section_order);

  auto doc = toml_document_create();
  if (doc == nullptr) {
    if (out_error != nullptr) {
      *out_error = "Failed to allocate TOML document";
    }
    return nullptr;
  }

  // 1. [Core]
  toml_document_set_section_comment(
      doc.get(), "Core",
      "LinApple Core Emulation Configuration\n"
      "Apple II system model, CPU execution speed, and startup options");
  auto* core = toml_get_or_create_table(doc.get(), "Core");
  std::string mach = get_val(ini, "Configuration", "Computer Emulation");
  if (mach.empty()) {
    mach = get_val(ini, "Configuration", "Machine Type", "3");
  }
  toml_table_set_string(core, "Machine", map_machine_type(mach), "",
                        "Apple II machine model:\n"
                        "  Allowed: Apple ][, Apple ][+, Apple //e, Apple //e "
                        "Enhanced, Apple //e "
                        "(Japanese)\n"
                        "  Default: Apple //e Enhanced");

  double speed = 1.0;
  if (has_key(ini, "Configuration", "Emulation Speed")) {
    int64_t raw_speed = get_int(ini, "Configuration", "Emulation Speed", 10);
    speed = (raw_speed == 10) ? 1.0 : (static_cast<double>(raw_speed) / 10.0);
  } else if (has_key(ini, "Configuration", "EmulationSpeed")) {
    speed = get_double(ini, "Configuration", "EmulationSpeed", 1.0);
  }
  toml_table_set_double(
      core, "EmulationSpeed", speed, "",
      "Emulation speed multiplier (1.0 = 1.023 MHz, 0.1 to 100.0):\n"
      "  Default: 1.0");

  toml_table_set_bool(
      core, "EnhanceDiskSpeed",
      get_bool(ini, "Configuration", "Enhance Disk Speed", true), "",
      "Accelerate CPU execution during disk accesses:\n"
      "  Allowed: true, false\n"
      "  Default: true");

  toml_table_set_bool(core, "BootOnStartup",
                      get_bool(ini, "Configuration", "Boot at Startup", false),
                      "",
                      "Automatically boot floppy in Slot 6 on startup:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");

  toml_table_set_bool(
      core, "SaveStateOnExit",
      get_bool(ini, "Configuration", "Save State On Exit", false), "",
      "Restore and save emulator state on exit:\n"
      "  Allowed: true, false\n"
      "  Default: false");

  const bool disable_dbg =
      get_bool(ini, "Configuration", "Disable Debugger", false);
  toml_table_set_bool(core, "EnableDebugger", !disable_dbg, "",
                      "Integrated 6502 assembly debugger:\n"
                      "  Allowed: true, false\n"
                      "  Default: false");

  const std::string basic_sync =
      get_val(ini, "Configuration", "Basic Live Sync File");
  if (!basic_sync.empty()) {
    toml_table_set_string(
        core, "BasicLiveSyncFile", basic_sync, "",
        "Bidirectional Applesoft BASIC live sync path (leave empty to "
        "disable):");
  }

  const int64_t basic_lines =
      get_int(ini, "Configuration", "Basic Line Numbering", 0);
  toml_table_set_string(core, "BasicLineNumbering",
                        (basic_lines == 1) ? "Positional" : "Explicit", "",
                        "Applesoft BASIC live sync line numbering:\n"
                        "  Allowed: Explicit, Positional\n"
                        "  Default: Positional");

  // 2. [Video]
  toml_document_set_section_comment(
      doc.get(), "Video",
      "Video Display & Rendering Options\n"
      "Video standards, color palettes, and monitor emulation");
  auto* video = toml_get_or_create_table(doc.get(), "Video");
  toml_table_set_string(
      video, "VideoStandard",
      map_video_standard(get_val(ini, "Configuration", "Video Standard")), "",
      "Video timing standard:\n"
      "  Allowed: NTSC, PAL\n"
      "  Default: NTSC");

  toml_table_set_string(
      video, "VideoEmulation",
      map_video_emulation(
          get_val(ini, "Configuration", "Video Emulation", "1")),
      "",
      "Display emulation color palette mode:\n"
      "  Allowed: Color Standard, Color Text Optimized, Color TV Emulation, "
      "Color Half-Shift, Monochrome Custom, Monochrome Amber, Monochrome "
      "Green, "
      "Monochrome White\n"
      "  Default: Color Standard");

  toml_table_set_string(
      video, "MonochromeColor",
      get_val(ini, "Configuration", "Monochrome Color", "#C0C0C0"), "",
      "Custom monochrome RGB hex code:\n"
      "  Default: #C0C0C0");

  // 3. [Frontend]
  toml_document_set_section_comment(
      doc.get(), "Frontend",
      "Host Presentation & Frontend Configuration\n"
      "Manages host window sizing, scaling, rendering pipelines, and display "
      "overlays\nacross graphical (SDL) and terminal (TUI) frontends.");
  auto* frontend = toml_get_or_create_table(doc.get(), "Frontend");
  toml_table_set_double(frontend, "ScreenFactor",
                        get_double(ini, "Configuration", "Screen factor", 1.0),
                        "",
                        "Window scaling factor (1.0 to 10.0):\n"
                        "  Default: 2.0");

  if (has_key(ini, "Configuration", "Screen Width")) {
    toml_table_set_int(frontend, "ScreenWidth",
                       get_int(ini, "Configuration", "Screen Width", 560), "",
                       "Custom window width in pixels:");
  }
  if (has_key(ini, "Configuration", "Screen Height")) {
    toml_table_set_int(frontend, "ScreenHeight",
                       get_int(ini, "Configuration", "Screen Height", 384), "",
                       "Custom window height in pixels:");
  }

  toml_table_set_bool(frontend, "Fullscreen",
                      get_bool(ini, "Configuration", "Fullscreen", false), "",
                      "Launch emulator in fullscreen mode:\n"
                      "  Allowed: true, false\n"
                      "  Default: false");

  const bool singlethreaded =
      get_bool(ini, "Configuration", "Singlethreaded", false);
  toml_table_set_bool(frontend, "Multithreaded", !singlethreaded, "",
                      "Multithreaded video rendering:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");

  toml_table_set_bool(frontend, "ShowLeds",
                      get_bool(ini, "Configuration", "Show Leds", true), "",
                      "On-screen disk activity LED indicator overlay:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");

  std::string tui_mode = get_val(ini, "Configuration", "TUI Render Mode");
  if (to_lower(tui_mode) == "block") {
    toml_table_set_string(frontend, "RenderMode", "Block", "",
                          "Terminal UI (TUI) rendering mode:\n"
                          "  Allowed: Smart, Block\n"
                          "  Default: Smart");
  } else {
    toml_table_set_string(frontend, "RenderMode", "Smart", "",
                          "Terminal UI (TUI) rendering mode:\n"
                          "  Allowed: Smart, Block\n"
                          "  Default: Smart");
  }

  toml_table_set_bool(frontend, "EnableHotkeys",
                      get_bool(ini, "Configuration", "Enable Hotkeys", true),
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
  toml_table_set_bool(audio, "SoundEmulation",
                      get_bool(ini, "Configuration", "Sound Emulation", true),
                      "",
                      "Enable host audio output:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");

  const int64_t spk_vol =
      has_key(ini, "Configuration", "Speaker Volume")
          ? scale_volume(get_int(ini, "Configuration", "Speaker Volume", 15))
          : 50;
  toml_table_set_int(audio, "SpeakerVolume", spk_vol, "",
                     "Apple II built-in speaker volume (0-100):\n"
                     "  Range: 0 to 100\n"
                     "  Default: 50");

  // 5. [Input]
  toml_document_set_section_comment(
      doc.get(), "Input",
      "Input Device Configuration\n"
      "Keyboard layout, mapping modes, and joystick calibration");
  auto* input = toml_get_or_create_table(doc.get(), "Input");
  toml_table_set_string(
      input, "KeyboardType",
      map_keyboard_layout(get_val(ini, "Configuration", "Keyboard Type", "0")),
      "",
      "Keyboard layout (Auto selects layout based on host system settings):\n"
      "  Allowed: Auto, US, UK, French, German, Spanish\n"
      "  Default: Auto");

  toml_table_set_string(
      input, "KeyboardRockerSwitch",
      map_keyboard_rocker(
          get_val(ini, "Configuration", "Keyboard Rocker Switch", "0")),
      "",
      "Keyboard rocker switch:\n"
      "  Allowed: US, Local\n"
      "  Default: US");

  toml_table_set_string(
      input, "MappingMode",
      map_key_mapping_mode(get_val(ini, "Configuration", "Mapping Mode", "0")),
      "",
      "Key mapping mode (Symbolic translates host characters to Apple II;\n"
      "Positional maps physical host key locations to Apple II layout for "
      "games):\n"
      "  Allowed: Symbolic, Positional\n"
      "  Default: Symbolic");

  toml_table_set_string(
      input, "CapsLockMode",
      map_caps_lock_mode(get_val(ini, "Configuration", "Caps Lock Mode", "0")),
      "",
      "Caps lock handling mode:\n"
      "  Allowed: Host, Emulated\n"
      "  Default: Host");

  std::string qs_mod =
      get_val(ini, "Configuration", "Quick Save Modifier", "Alt");
  toml_table_set_string(input, "QuickSaveModifier",
                        qs_mod.empty() ? "Alt" : qs_mod, "",
                        "Keyboard shortcut modifier for quick-save:\n"
                        "  Allowed: Alt, Ctrl, AltCtrl, Disabled\n"
                        "  Default: Alt");

  toml_table_set_bool(input, "ScrollLockToggle",
                      get_bool(ini, "Configuration", "ScrollLock Toggle", true),
                      "",
                      "ScrollLock toggles unthrottled emulation speed:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");

  toml_table_set_string(
      input, "Joystick0Mode",
      map_joystick_mode(get_val(ini, "Configuration", "Joystick 0", "2")), "",
      "Joystick 0 emulation mode:\n"
      "  Allowed: Disabled, Host Gamepad, Keyboard Standard, Keyboard "
      "Centered, "
      "Mouse Joystick\n"
      "  Default: Keyboard Standard");
  toml_table_set_string(
      input, "Joystick1Mode",
      map_joystick_mode(get_val(ini, "Configuration", "Joystick 1", "0")), "",
      "Joystick 1 emulation mode:\n"
      "  Allowed: Disabled, Host Gamepad, Keyboard Standard, Keyboard "
      "Centered, "
      "Mouse Joystick\n"
      "  Default: Disabled");

  toml_table_set_int(input, "Joy0Index",
                     get_int(ini, "Configuration", "Joystick 0 Index", 0), "",
                     "Host gamepad device index for Joystick 0:\n"
                     "  Default: 0");
  toml_table_set_int(input, "Joy1Index",
                     get_int(ini, "Configuration", "Joystick 1 Index", 1), "",
                     "Host gamepad device index for Joystick 1:\n"
                     "  Default: 1");

  toml_table_set_int(
      input, "Joy0Button1",
      get_int(ini, "Configuration", "Joystick 0 Button 1", 0), "",
      "Host gamepad button mapping for Apple II Button 0 (PB0):\n"
      "  Default: 0");
  toml_table_set_int(
      input, "Joy0Button2",
      get_int(ini, "Configuration", "Joystick 0 Button 2", 1), "",
      "Host gamepad button mapping for Apple II Button 1 (PB1):\n"
      "  Default: 1");
  toml_table_set_int(
      input, "Joy1Button1",
      get_int(ini, "Configuration", "Joystick 1 Button 1", 0), "",
      "Host gamepad button mapping for Joystick 1 Apple II Button 0 (PB0):\n"
      "  Default: 0");
  toml_table_set_int(
      input, "Joy1Button2",
      get_int(ini, "Configuration", "Joystick 1 Button 2", 1), "",
      "Host gamepad button mapping for Joystick 1 Apple II Button 1 (PB1):\n"
      "  Default: 1");

  toml_table_set_int(input, "Joy0Axis0",
                     get_int(ini, "Configuration", "Joystick 0 Axis 0", 0), "",
                     "Host gamepad axis mapping for Joystick 0 X:\n"
                     "  Default: 0");
  toml_table_set_int(input, "Joy0Axis1",
                     get_int(ini, "Configuration", "Joystick 0 Axis 1", 1), "",
                     "Host gamepad axis mapping for Joystick 0 Y:\n"
                     "  Default: 1");
  toml_table_set_int(input, "Joy1Axis0",
                     get_int(ini, "Configuration", "Joystick 1 Axis 0", 0), "",
                     "Host gamepad axis mapping for Joystick 1 X:\n"
                     "  Default: 0");
  toml_table_set_int(input, "Joy1Axis1",
                     get_int(ini, "Configuration", "Joystick 1 Axis 1", 1), "",
                     "Host gamepad axis mapping for Joystick 1 Y:\n"
                     "  Default: 1");

  const int64_t pdl_x = has_key(ini, "Configuration", "PDL X-Trim")
                            ? get_int(ini, "Configuration", "PDL X-Trim", 0)
                            : get_int(ini, "Configuration", "Paddle 0 Trim", 0);
  toml_table_set_int(input, "PdlXTrim", pdl_x, "",
                     "Paddle X trim offset (-128 to 127):\n"
                     "  Range: -128 to 127\n"
                     "  Default: 0");
  const int64_t pdl_y = has_key(ini, "Configuration", "PDL Y-Trim")
                            ? get_int(ini, "Configuration", "PDL Y-Trim", 0)
                            : get_int(ini, "Configuration", "Paddle 1 Trim", 0);
  toml_table_set_int(input, "PdlYTrim", pdl_y, "",
                     "Paddle Y trim offset (-128 to 127):\n"
                     "  Range: -128 to 127\n"
                     "  Default: 0");

  toml_table_set_bool(
      input, "JoyExitEnable",
      get_bool(ini, "Configuration", "Joystick Exit Enable", false), "",
      "Exit emulator on simultaneous gamepad button chord:\n"
      "  Allowed: true, false\n"
      "  Default: false");
  toml_table_set_int(input, "JoyExitButton0",
                     get_int(ini, "Configuration", "Joystick Exit Button 0", 8),
                     "",
                     "Exit chord first button index:\n"
                     "  Default: 8");
  toml_table_set_int(input, "JoyExitButton1",
                     get_int(ini, "Configuration", "Joystick Exit Button 1", 9),
                     "",
                     "Exit chord second button index:\n"
                     "  Default: 9");
  toml_table_set_bool(input, "MouseCapture",
                      get_bool(ini, "Configuration", "Mouse Capture", true), "",
                      "Capture host mouse pointer on window click:\n"
                      "  Allowed: true, false\n"
                      "  Default: true");

  // 6. [Slots]
  toml_document_set_section_comment(
      doc.get(), "Slots",
      "Expansion Slot Card Assignments (Slots 1-7)\n"
      "Run 'linapple --list-hardware' to view all available peripherals.");
  auto* slots = toml_get_or_create_table(doc.get(), "Slots");
  std::vector<std::string> slot_cards(
      8, peripheral_card_to_string(PeripheralCardType_t::Empty));
  slot_cards[1] =
      peripheral_card_to_string(PeripheralCardType_t::ParallelPrinter);
  slot_cards[2] = peripheral_card_to_string(PeripheralCardType_t::SuperSerial);
  slot_cards[3] = peripheral_card_to_string(PeripheralCardType_t::Empty);
  slot_cards[4] = peripheral_card_to_string(PeripheralCardType_t::Mockingboard);
  slot_cards[5] = peripheral_card_to_string(PeripheralCardType_t::Mockingboard);
  slot_cards[6] = peripheral_card_to_string(PeripheralCardType_t::DiskII);
  slot_cards[7] = peripheral_card_to_string(PeripheralCardType_t::Harddisk);

  std::vector<bool> slot_explicit(8, false);
  if (ini.find("Slots") != ini.end()) {
    for (size_t s = 1; s <= 7; ++s) {
      const std::string s_key = "Slot " + std::to_string(s);
      const std::string s_alt = "Slot" + std::to_string(s);
      std::string card = get_val(ini, "Slots", s_key);
      if (card.empty()) {
        card = get_val(ini, "Slots", s_alt);
      }
      if (!card.empty()) {
        slot_cards[s] = map_peripheral_card(card);
        slot_explicit[s] = true;
      }
    }
  }

  // Apply legacy slot toggles for any slot not explicitly assigned in [Slots]
  if (!slot_explicit[4] &&
      get_bool(ini, "Configuration", "Mouse in slot 4", false)) {
    slot_cards[4] = peripheral_card_to_string(PeripheralCardType_t::Mouse);
  }
  if (!slot_explicit[7] &&
      !get_bool(ini, "Configuration", "Harddisk Enable", false)) {
    slot_cards[7] = peripheral_card_to_string(PeripheralCardType_t::Empty);
  }
  const int64_t clock_slot = get_int(ini, "Configuration", "Clock Enable", 0);
  if (clock_slot >= 1 && clock_slot <= 7 &&
      !slot_explicit[static_cast<size_t>(clock_slot)]) {
    slot_cards[static_cast<size_t>(clock_slot)] =
        peripheral_card_to_string(PeripheralCardType_t::Clock);
  }

  for (size_t s = 1; s <= 7; ++s) {
    toml_table_set_string(slots, "Slot" + std::to_string(s), slot_cards[s]);
  }

  // 7. Peripherals: [Peripheral.ParallelPrinter]
  std::map<std::string, std::string> printer_overrides;
  printer_overrides["Filename"] =
      get_val(ini, "Configuration", "Parallel Printer Filename", "Printer.txt");
  printer_overrides["IdleLimit"] =
      std::to_string(get_int(ini, "Configuration", "Printer idle limit", 10));
  printer_overrides["Append"] =
      get_bool(ini, "Configuration", "Append to printer file", true) ? "true"
                                                                     : "false";
  peripheral_populate_toml_table(doc.get(), "Peripheral.ParallelPrinter",
                                 "linapple.printer", printer_overrides);

  // 8. Peripherals: [Peripheral.SuperSerial]
  std::map<std::string, std::string> ssc_overrides;
  ssc_overrides["Port"] =
      get_val(ini, "Configuration", "Serial Port", "/dev/null");
  ssc_overrides["Baud"] =
      std::to_string(get_int(ini, "Configuration", "Serial Baud", 9600));
  ssc_overrides["Loopback"] =
      get_bool(ini, "Configuration", "Serial Loopback", false) ? "true"
                                                               : "false";
  peripheral_populate_toml_table(doc.get(), "Peripheral.SuperSerial",
                                 "linapple.ssc", ssc_overrides);

  // 9. Peripherals: [Peripheral.DiskII]
  std::map<std::string, std::string> disk_overrides;
  disk_overrides["Drive1"] =
      get_val_fallback(ini, "Preferences", "Disk Image 1");
  disk_overrides["Drive2"] =
      get_val_fallback(ini, "Preferences", "Disk Image 2");
  disk_overrides["FastDisk"] =
      get_bool(ini, "Configuration", "Enhance Disk Speed", true) ? "true"
                                                                 : "false";
  peripheral_populate_toml_table(doc.get(), "Peripheral.DiskII",
                                 "linapple.disk_II", disk_overrides);

  // 10. Peripherals: [Peripheral.Harddisk]
  std::map<std::string, std::string> hd_overrides;
  hd_overrides["Drive1"] =
      get_val_fallback(ini, "Preferences", "Harddisk Image 1");
  hd_overrides["Drive2"] =
      get_val_fallback(ini, "Preferences", "Harddisk Image 2");
  peripheral_populate_toml_table(doc.get(), "Peripheral.Harddisk",
                                 "linapple.harddisk", hd_overrides);

  // 11. Peripherals: [Peripheral.Mockingboard]
  std::map<std::string, std::string> mb_overrides;
  const int64_t mb_vol = has_key(ini, "Configuration", "Mockingboard Volume")
                             ? scale_volume(get_int(ini, "Configuration",
                                                    "Mockingboard Volume", 15))
                             : 50;
  mb_overrides["Volume"] = std::to_string(mb_vol);
  const std::string sc_type =
      map_soundcard_type(get_val(ini, "Configuration", "Soundcard Type", "2"));
  if (sc_type == "Phasor") {
    mb_overrides["Type"] = "Phasor";
  } else {
    mb_overrides["Type"] = "Mockingboard";
  }
  peripheral_populate_toml_table(doc.get(), "Peripheral.Mockingboard",
                                 "linapple.mockingboard", mb_overrides);

  // 12. [Network]
  toml_document_set_section_comment(
      doc.get(), "Network", "Network & Remote Disk Image Server Configuration");
  auto* net = toml_get_or_create_table(doc.get(), "Network");
  toml_table_set_string(
      net, "FtpServer",
      get_val_fallback(ini, "Preferences", "FTP Server",
                       "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/"),
      "", "Remote FTP server directory URL for 5.25-inch disk images:");
  toml_table_set_string(
      net, "FtpServerHdd",
      get_val_fallback(ini, "Preferences", "FTP ServerHDD",
                       "ftp://ftp.apple.asimov.net/pub/apple_II/images/"),
      "", "Remote FTP server directory URL for hard disk images:");
  toml_table_set_string(net, "FtpUserPass",
                        get_val_fallback(ini, "Preferences", "FTP UserPass",
                                         "anonymous:my-mail@mail.com"),
                        "",
                        "FTP authentication credentials (username:password):");
  toml_table_set_string(
      net, "FtpLocalDir", get_val_fallback(ini, "Preferences", "FTP Local Dir"),
      "", "Local cache directory path for downloaded disk images:");

  // 13. [Preferences] (preserve paths if specified)
  const std::string s6_dir =
      get_val_fallback(ini, "Preferences", "Slot 6 Directory");
  const std::string hdv_dir =
      get_val_fallback(ini, "Preferences", "HDV Starting Directory");
  const std::string ss_dir =
      get_val_fallback(ini, "Preferences", "Save State Directory");
  const std::string ss_file =
      get_val_fallback(ini, "Preferences", "Save State Filename");
  if (!s6_dir.empty() || !hdv_dir.empty() || !ss_dir.empty() ||
      !ss_file.empty()) {
    toml_document_set_section_comment(doc.get(), "Preferences",
                                      "Emulation Directory & Path Preferences");
    auto* pref = toml_get_or_create_table(doc.get(), "Preferences");
    if (!s6_dir.empty()) {
      toml_table_set_string(pref, "Slot6Directory", s6_dir);
    }
    if (!hdv_dir.empty()) {
      toml_table_set_string(pref, "HdvDirectory", hdv_dir);
    }
    if (!ss_dir.empty()) {
      toml_table_set_string(pref, "SaveStateDirectory", ss_dir);
    }
    if (!ss_file.empty()) {
      toml_table_set_string(pref, "SaveStateFilename", ss_file);
    }
  }

  // 14. Preserve custom sections like [Keyboard.Custom], [Custom ROM], etc.
  for (const auto& sec_pair : ini) {
    const std::string& sec_name = sec_pair.first;
    if (to_lower(sec_name) == "configuration" ||
        to_lower(sec_name) == "preferences" || to_lower(sec_name) == "slots") {
      continue;
    }

    auto* tbl = toml_get_or_create_table(doc.get(), sec_name);
    for (const auto& kv : sec_pair.second) {
      toml_table_set_string(tbl, kv.first, kv.second);
    }
  }

  return doc;
}

auto config_migrate_legacy_ini(const std::string& ini_content,
                               LinAppleConfig_t* out_config,
                               std::string* out_error) -> bool {
  if (out_config == nullptr) {
    if (out_error != nullptr) {
      *out_error = "out_config pointer is null";
    }
    return false;
  }

  *out_config = config_defaults();

  auto doc = config_migrate_legacy_to_toml(ini_content, out_error);
  if (doc == nullptr) {
    return false;
  }

  std::vector<ConfigValidationError_t> val_errs;
  if (!config_from_toml(doc.get(), out_config, &val_errs)) {
    if (out_error != nullptr) {
      *out_error = "Migrated configuration failed validation";
      if (!val_errs.empty()) {
        *out_error += ": [" + val_errs[0].section + "]." + val_errs[0].key +
                      ": " + val_errs[0].message;
      }
    }
    return false;
  }

  return true;
}

auto config_upgrade_legacy(const std::string& legacy_conf_path,
                           const std::string& target_toml_path,
                           std::string* out_error) -> bool {
  if (legacy_conf_path.empty()) {
    if (out_error != nullptr) {
      *out_error = "Legacy configuration path is empty";
    }
    return false;
  }

  if (access(legacy_conf_path.c_str(), R_OK) != 0) {
    if (out_error != nullptr) {
      *out_error = "Legacy configuration file not found or inaccessible: " +
                   legacy_conf_path;
    }
    return false;
  }

  std::ifstream stream(legacy_conf_path, std::ios::in | std::ios::binary);
  if (!stream.is_open()) {
    if (out_error != nullptr) {
      *out_error =
          "Failed to open legacy configuration file: " + legacy_conf_path;
    }
    return false;
  }

  std::string content((std::istreambuf_iterator<char>(stream)),
                      std::istreambuf_iterator<char>());
  stream.close();

  auto doc = config_migrate_legacy_to_toml(content, out_error);
  if (doc == nullptr) {
    return false;
  }

  const std::string dest_path = target_toml_path.empty()
                                    ? config_get_default_user_path()
                                    : target_toml_path;

  Path::ensure_dir_exists(dest_path);

  std::string save_err;
  if (!toml_document_save_file(doc.get(), dest_path, &save_err)) {
    if (out_error != nullptr) {
      *out_error = "Failed to save upgraded TOML configuration to '" +
                   dest_path + "': " + save_err;
    }
    return false;
  }

  return true;
}
