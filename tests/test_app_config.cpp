// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstring>
#include <string>

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
