// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>

#include "LinAppleCore.h"
#include "apple2/Apple2Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"

TEST_CASE("AppConfig_t: Initialization") {
  AppConfig_t config = {};
  app_config_default(&config);

  CHECK(config.intent == INTENT_RUN);
  CHECK(config.apple2_type == A2TYPE_APPLE2EENHANCED);
  CHECK(config.is_pal == false);
  CHECK(config.is_fullscreen == false);
  CHECK(config.is_list_hardware == false);
  CHECK(config.disk_path[0][0] == '\0');
  CHECK(config.disk_path[1][0] == '\0');
  CHECK(config.program_path[0] == '\0');
  CHECK(config.config_path[0] == '\0');
  CHECK(config.hardware_info_name[0] == '\0');
}

TEST_CASE("AppConfig_t: Manual Population") {
  AppConfig_t config = {};
  app_config_default(&config);

  config.intent = INTENT_DIAGNOSTIC;
  util_safe_strcpy(config.disk_path[0].data(), "test.dsk", path_max_len);
  config.apple2_type = A2TYPE_APPLE2PLUS;
  config.is_pal = true;

  CHECK(config.intent == INTENT_DIAGNOSTIC);
  CHECK(strcmp(config.disk_path[0].data(), "test.dsk") == 0);
  CHECK(config.apple2_type == A2TYPE_APPLE2PLUS);
  CHECK(config.is_pal == true);
}

TEST_CASE("Registry: Mouse Capture Key Definition") {
  CHECK(strcmp(REGVALUE_MOUSE_CAPTURE, "Mouse Capture") == 0);
  CHECK(strcmp(REGVALUE_MOUSE_IN_SLOT4, "Mouse in slot 4") == 0);
}

TEST_CASE("Registry: Joystick Config Aliases") {
  auto& reg = Configuration_t::instance();
  reg.set_int("Configuration", "Joy0Axis0", 4);
  reg.set_int("Configuration", "Joy0Axis1", 5);
  reg.set_int("Configuration", "Joy0Button1", 3);

  // Canonical queries should transparently resolve legacy alias keys
  uint32_t val = 0;
  CHECK(config_load_int("Configuration", REGVALUE_JOY_AXIS1_0, &val));
  CHECK(val == 4);

  CHECK(config_load_int("Configuration", REGVALUE_JOY_AXIS1_1, &val));
  CHECK(val == 5);

  CHECK(config_load_int("Configuration", REGVALUE_JOY_BUTTON1_1, &val));
  CHECK(val == 3);

  // Setting canonical key should resolve when queried with legacy alias
  reg.set_int("Configuration", "Joystick 1 Axis 0", 2);
  CHECK(config_load_int("Configuration", "Joy1Axis0", &val));
  CHECK(val == 2);
}

TEST_CASE("Registry: Caps Lock Mode Config") {
  auto& reg = Configuration_t::instance();
  reg.set_int("Keyboard", "Caps Lock Mode", 1);
  uint32_t val = 0;
  CHECK(config_load_int("Keyboard", "Caps Lock Mode", &val));
  CHECK(val == 1);
}

TEST_CASE("Registry: Snake-case cfg_* constants and section names") {
  CHECK(strcmp(cfg_sec_configuration, "Configuration") == 0);
  CHECK(strcmp(cfg_sec_slots, "Slots") == 0);
  CHECK(strcmp(cfg_sec_preferences, "Preferences") == 0);

  CHECK(strcmp(cfg_mouse_capture, "Mouse Capture") == 0);
  CHECK(strcmp(cfg_disk_image1, "Disk Image 1") == 0);
  CHECK(strcmp(cfg_disk_image2, "Disk Image 2") == 0);

  // Backward compatibility aliases match cfg_* constants
  CHECK(REGVALUE_MOUSE_CAPTURE == cfg_mouse_capture);
  CHECK(REGVALUE_DISK_IMAGE1 == cfg_disk_image1);
  CHECK(REGVALUE_DISK_IMAGE2 == cfg_disk_image2);
}

TEST_CASE("Registry: Symmetric load and save free functions") {
  save("TestIntKey", 42U);
  uint32_t int_val = 0;
  CHECK(load("TestIntKey", &int_val));
  CHECK(int_val == 42U);

  save("TestSignedIntKey", 12345);
  uint32_t signed_int_val = 0;
  CHECK(load("TestSignedIntKey", &signed_int_val));
  CHECK(signed_int_val == 12345U);

  save("TestBoolKey", true);
  bool bool_val = false;
  CHECK(load("TestBoolKey", &bool_val));
  CHECK(bool_val == true);

  save("TestBoolFalseKey", false);
  CHECK(load("TestBoolFalseKey", &bool_val));
  CHECK(bool_val == false);

  save("TestStrKey", "LinappleRocks");
  std::string str_val;
  CHECK(load("TestStrKey", &str_val));
  CHECK(str_val == "LinappleRocks");

  save("TestStdStrKey", std::string("AppleIIe"));
  CHECK(load("TestStdStrKey", &str_val));
  CHECK(str_val == "AppleIIe");
}

TEST_CASE("Registry: Direct const char* overloads") {
  auto& reg = Configuration_t::instance();
  reg.set_string("CustomSection", "Greeting", "Hello");
  reg.set_int("CustomSection", "Count", 7);
  reg.set_bool("CustomSection", "Active", true);

  CHECK(reg.get_string("CustomSection", "Greeting") == "Hello");
  CHECK(reg.get_int("CustomSection", "Count") == 7);
  CHECK(reg.get_bool("CustomSection", "Active") == true);

  CHECK(reg.get_string("MissingSection", "MissingGreeting", "Default") ==
        "Default");
  CHECK(reg.get_int("MissingSection", "MissingCount", 99) == 99);
  CHECK(reg.get_bool("MissingSection", "MissingActive", false) == false);
}

TEST_CASE("Registry: Procedural API and struct Configuration_t") {
  Configuration_t& cfg = config_instance();
  CHECK(&cfg == &Configuration_t::instance());

  // Direct public struct member access (procedural C-style)
  CHECK(cfg.path == config_get_path());
  CHECK(cfg.data.find("Configuration") != cfg.data.end());

  config_set_string("ProceduralSection", "Key", "Val");
  config_set_int("ProceduralSection", "Number", 123);
  config_set_bool("ProceduralSection", "Flag", true);

  CHECK(config_get_string("ProceduralSection", "Key") == "Val");
  CHECK(config_get_int("ProceduralSection", "Number") == 123);
  CHECK(config_get_bool("ProceduralSection", "Flag") == true);
}

TEST_CASE("Registry: Negative and error-path queries") {
  // Non-existent key must return false and leave output parameters unchanged
  uint32_t int_target = 0xDEADBEEFU;
  CHECK_FALSE(load("DefinitelyNonExistentKey999", &int_target));
  CHECK(int_target == 0xDEADBEEFU);

  bool bool_target = true;
  CHECK_FALSE(load("DefinitelyNonExistentKey999", &bool_target));
  CHECK(bool_target == true);

  std::string str_target = "PreservedValue";
  CHECK_FALSE(load("DefinitelyNonExistentKey999", &str_target));
  CHECK(str_target == "PreservedValue");

  // Type mismatch: non-numeric string should return false on int query
  save("InvalidIntKey", "NotANumber");
  int_target = 777U;
  CHECK_FALSE(load("InvalidIntKey", &int_target));
  CHECK(int_target == 777U);
}

TEST_CASE("Registry: Null-safety boundary conditions") {
  uint32_t val = 123;
  CHECK_FALSE(config_load_int(nullptr, "key", &val));
  CHECK_FALSE(config_load_int("section", nullptr, &val));
  CHECK_FALSE(config_load_int("section", "key", nullptr));

  bool bval = false;
  CHECK_FALSE(config_load_bool(nullptr, "key", &bval));
  CHECK_FALSE(config_load_bool("section", nullptr, &bval));
  CHECK_FALSE(config_load_bool("section", "key", nullptr));

  std::string sval;
  CHECK_FALSE(config_load_string(nullptr, "key", &sval));
  CHECK_FALSE(config_load_string("section", nullptr, &sval));
  CHECK_FALSE(config_load_string("section", "key", nullptr));

  // Null parameter saves should safely no-op without crashes
  config_save_int(nullptr, "key", 42);
  config_save_int("section", nullptr, 42);
  config_save_bool(nullptr, "key", true);
  config_save_bool("section", nullptr, true);
  config_save_string(nullptr, "key", "val");
  config_save_string("section", nullptr, "val");
  config_save_string("section", "key", nullptr);

  auto& reg = Configuration_t::instance();
  CHECK(reg.get_string(nullptr, nullptr, "Fallback") == "Fallback");
  CHECK(reg.get_int(nullptr, nullptr, 999) == 999);
  CHECK(reg.get_bool(nullptr, nullptr, true) == true);

  reg.set_string(nullptr, nullptr, nullptr);
  reg.set_int(nullptr, nullptr, 0);
  reg.set_bool(nullptr, nullptr, false);
}

TEST_CASE("AppConfig_t: Type equivalence to Configuration_t") {
  static_assert(std::is_same<AppConfig_t, Configuration_t>::value,
                "AppConfig_t must be an alias for Configuration_t");
  CHECK((std::is_same<AppConfig_t, Configuration_t>::value));
}

TEST_CASE("Configuration_t: Typed field synchronization via sync_to_data") {
  Configuration_t cfg = {};
  cfg.apple2_type = A2TYPE_APPLE2PLUS;
  cfg.is_fullscreen = true;
  cfg.is_pal = true;
  cfg.disable_debugger = true;
  util_safe_strcpy(cfg.disk_path.at(0).data(), "boot.dsk", path_max_len);
  util_safe_strcpy(cfg.harddisk_path.at(0).data(), "hdd.hdv", path_max_len);
  util_safe_strcpy(cfg.snapshot_path.data(), "state.snap", path_max_len);
  util_safe_strcpy(cfg.basic_sync_file.data(), "code.bas", path_max_len);
  cfg.basic_line_mode = 1;
  cfg.tui_render_mode = TUI_RENDER_BLOCK;
  cfg.tui_render_mode_explicit = true;

  cfg.sync_to_data();

  CHECK(cfg.get_int(cfg_sec_configuration, cfg_computer_emulation) == 1);
  CHECK(cfg.get_bool(cfg_sec_configuration, "Fullscreen") == true);
  CHECK(cfg.get_int(cfg_sec_configuration, "Video Emulation") == 2);
  CHECK(cfg.get_bool(cfg_sec_configuration, cfg_disable_debugger) == true);
  CHECK(cfg.get_string(cfg_sec_slots, cfg_disk_image1) == "boot.dsk");
  CHECK(cfg.get_string(cfg_sec_preferences, cfg_hdd_image1) == "hdd.hdv");
  CHECK(cfg.get_string(cfg_sec_preferences, cfg_hdd_enabled) == "1");
  CHECK(cfg.get_string(cfg_sec_configuration, cfg_savestate_filename) ==
        "state.snap");
  CHECK(cfg.get_string(cfg_sec_configuration, cfg_basic_sync_file) ==
        "code.bas");
  CHECK(cfg.get_int(cfg_sec_configuration, cfg_basic_line_mode) == 1);
  CHECK(cfg.get_string(cfg_sec_configuration, cfg_tui_render_mode) == "block");
}

TEST_CASE("Configuration_t: Setter synchronization to typed fields") {
  Configuration_t cfg = {};
  cfg.set_int(cfg_sec_configuration, cfg_computer_emulation, 2);
  CHECK(cfg.apple2_type == A2TYPE_APPLE2E);

  cfg.set_bool(cfg_sec_configuration, "Fullscreen", true);
  CHECK(cfg.is_fullscreen == true);

  cfg.set_int(cfg_sec_configuration, "Video Emulation", 2);
  CHECK(cfg.is_pal == true);

  cfg.set_string(cfg_sec_slots, cfg_disk_image1, "new_disk.dsk");
  CHECK(std::string(cfg.disk_path.at(0).data()) == "new_disk.dsk");

  cfg.set_string(cfg_sec_preferences, cfg_hdd_image1, "new_hd.hdv");
  CHECK(std::string(cfg.harddisk_path.at(0).data()) == "new_hd.hdv");

  cfg.set_string(cfg_sec_configuration, cfg_savestate_filename,
                 "new_save.snap");
  CHECK(std::string(cfg.snapshot_path.data()) == "new_save.snap");

  cfg.set_string(cfg_sec_configuration, cfg_basic_sync_file, "live.bas");
  CHECK(std::string(cfg.basic_sync_file.data()) == "live.bas");

  cfg.set_int(cfg_sec_configuration, cfg_basic_line_mode, 0);
  CHECK(cfg.basic_line_mode == 0);

  cfg.set_bool(cfg_sec_configuration, cfg_disable_debugger, true);
  CHECK(cfg.disable_debugger == true);
}

TEST_CASE("Configuration_t: Load INI file populates typed fields") {
  const char* temp_filename = "test_config_sync.conf";
  {
    std::ofstream out(temp_filename);
    out << "[Configuration]\n"
        << "Computer Emulation = 0\n"
        << "Fullscreen = 1\n"
        << "Video Emulation = 2\n"
        << "Disable Debugger = 1\n"
        << "Save State Filename = loaded.snap\n"
        << "Basic Live Sync File = loaded.bas\n"
        << "Basic Line Numbering = 1\n"
        << "TUI Render Mode = block\n"
        << "\n[Slots]\n"
        << "Disk Image 1 = loaded_d1.dsk\n"
        << "\n[Preferences]\n"
        << "Harddisk Image 1 = loaded_hd1.hdv\n";
  }

  Configuration_t cfg = {};
  bool loaded = cfg.load(temp_filename);
  std::remove(temp_filename);

  REQUIRE(loaded);
  CHECK(cfg.apple2_type == A2TYPE_APPLE2);
  CHECK(cfg.is_fullscreen == true);
  CHECK(cfg.is_pal == true);
  CHECK(cfg.disable_debugger == true);
  CHECK(std::string(cfg.snapshot_path.data()) == "loaded.snap");
  CHECK(std::string(cfg.basic_sync_file.data()) == "loaded.bas");
  CHECK(cfg.basic_line_mode == 1);
  CHECK(cfg.tui_render_mode == TUI_RENDER_BLOCK);
  CHECK(std::string(cfg.disk_path.at(0).data()) == "loaded_d1.dsk");
  CHECK(std::string(cfg.harddisk_path.at(0).data()) == "loaded_hd1.hdv");
}

TEST_CASE(
    "Configuration_t: Explicit CLI options take precedence over INI file") {
  const char* temp_filename = "test_config_explicit.conf";
  {
    std::ofstream out(temp_filename);
    out << "[Configuration]\n"
        << "Fullscreen = 0\n"
        << "Video Emulation = 1\n";
  }

  Configuration_t cfg = {};
  cfg.is_fullscreen = true;
  cfg.is_fullscreen_explicit = true;
  cfg.is_pal = true;
  cfg.is_pal_explicit = true;

  bool loaded = cfg.load(temp_filename);
  std::remove(temp_filename);

  REQUIRE(loaded);
  CHECK(cfg.is_fullscreen == true);
  CHECK(cfg.is_pal == true);
}

TEST_CASE(
    "Configuration_t: Failed load does not mutate path or clear existing "
    "data") {
  Configuration_t cfg = {};
  cfg.set_path("/initial/path.conf");
  cfg.set_string("Section", "Key", "Value");

  bool loaded = cfg.load("/nonexistent/file/path.conf");
  CHECK_FALSE(loaded);
  CHECK(cfg.get_path() == "/initial/path.conf");
  CHECK(std::string(cfg.config_path.data()) == "/initial/path.conf");
  CHECK(cfg.get_string("Section", "Key") == "Value");
}

TEST_CASE("Configuration_t: load_defaults preserves configured path") {
  Configuration_t cfg = {};
  cfg.set_path("/saved/path.conf");
  cfg.load_defaults();

  CHECK(cfg.get_path() == "/saved/path.conf");
  CHECK(std::string(cfg.config_path.data()) == "/saved/path.conf");
  CHECK(cfg.apple2_type == A2TYPE_APPLE2EENHANCED);
}
