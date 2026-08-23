#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"

TEST_CASE("AppConfig_t: Initialization") {
  AppConfig_t config = {};
  AppConfig_Default(&config);

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
  AppConfig_Default(&config);

  config.intent = INTENT_DIAGNOSTIC;
  Util_SafeStrCpy(config.disk_path[0].data(), "test.dsk", path_max_len);
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

