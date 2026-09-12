// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>

#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppConfig.h"

TEST_CASE("AppArgs: Basic Parsing") {
  char* argv[] = {(char*)"linapple", (char*)"--d1", (char*)"disk1.dsk",
                  (char*)"--boot"};
  int argc = 4;
  AppConfig_t config = {};
  int res = app_args_parse(argc, argv, &config);

  CHECK(res == 0);
  CHECK(strcmp(config.disk_path[0].data(), "disk1.dsk") == 0);
  CHECK(config.is_boot == true);
  CHECK(config.intent == INTENT_RUN);
}

TEST_CASE("AppArgs: Diagnostic Intent") {
  char* argv[] = {(char*)"linapple", (char*)"--list-hardware"};
  int argc = 2;
  AppConfig_t config = {};
  int res = app_args_parse(argc, argv, &config);

  CHECK(res == 0);
  CHECK(config.is_list_hardware == true);
  CHECK(config.intent == INTENT_DIAGNOSTIC);
}

TEST_CASE("AppArgs: Frontend Pass-through") {
  // getopt_long might reorder argv, so we use a copy to be safe if we were to
  // reuse it
  char* argv[] = {(char*)"linapple", (char*)"--boot", (char*)"--wayland",
                  (char*)"pos1"};
  int argc = 4;
  AppConfig_t config = {};
  int res = app_args_parse(argc, argv, &config);

  CHECK(res == 0);
  CHECK(config.is_boot == true);
  CHECK(config.argc_extra == 2);
  CHECK(strcmp(config.argv_extra[0], "--wayland") == 0);
  CHECK(strcmp(config.argv_extra[1], "pos1") == 0);
}

TEST_CASE("AppArgs: Help Intent") {
  char* argv[] = {(char*)"linapple", (char*)"-h"};
  int argc = 2;
  AppConfig_t config = {};
  int res = app_args_parse(argc, argv, &config);

  CHECK(res == 0);
  CHECK(config.intent == INTENT_HELP);
}

TEST_CASE("AppArgs: Missing Argument error") {
  // --config requires an argument. Passing it as the last flag should trigger
  // an error.
  char* argv[] = {(char*)"linapple", (char*)"--config"};
  int argc = 2;
  AppConfig_t config = {};
  int res = app_args_parse(argc, argv, &config);

  CHECK(res != 0);
  CHECK(config.intent == INTENT_ERROR);
}

TEST_CASE("AppArgs: Caps Lock Mode Arguments") {
  SUBCASE("--caps-mode=emulated") {
    char* argv[] = {(char*)"linapple", (char*)"--caps-mode=emulated"};
    AppConfig_t config = {};
    int res = app_args_parse(2, argv, &config);
    CHECK(res == 0);
    CHECK(config.caps_lock_mode == CAPS_MODE_EMULATED);
  }

  SUBCASE("--caps-mode host") {
    char* argv[] = {(char*)"linapple", (char*)"--caps-mode", (char*)"host"};
    AppConfig_t config = {};
    int res = app_args_parse(3, argv, &config);
    CHECK(res == 0);
    CHECK(config.caps_lock_mode == CAPS_MODE_HOST);
  }
}

TEST_CASE("AppArgs: Upgrade Config Arguments") {
  char* argv[] = {(char*)"linapple", (char*)"--upgrade-config",
                  (char*)"/tmp/custom_target.toml"};
  AppConfig_t config = {};
  int result = app_args_parse(3, argv, &config);
  CHECK(result == 0);
  CHECK(config.is_upgrade_config == true);
  CHECK(strcmp(config.upgrade_target_path.data(), "/tmp/custom_target.toml") ==
        0);
  CHECK(config.intent == INTENT_DIAGNOSTIC);
}

TEST_CASE("AppArgs: Upgrade Config Default Target") {
  char* argv[] = {(char*)"linapple", (char*)"--upgrade-config"};
  AppConfig_t config = {};
  int result = app_args_parse(2, argv, &config);
  CHECK(result == 0);
  CHECK(config.is_upgrade_config == true);
  CHECK(config.upgrade_target_path[0] == '\0');
  CHECK(config.intent == INTENT_DIAGNOSTIC);
}

TEST_CASE("AppArgs: Explicit Negation and Override Flags") {
  SUBCASE("--no-autoboot") {
    char* argv[] = {(char*)"linapple", (char*)"--no-autoboot"};
    AppConfig_t config = {};
    int res = app_args_parse(2, argv, &config);
    CHECK(res == 0);
    CHECK(config.is_boot == false);
    CHECK(config.is_boot_explicit == true);
  }

  SUBCASE("--no-fullscreen") {
    char* argv[] = {(char*)"linapple", (char*)"--no-fullscreen"};
    AppConfig_t config = {};
    int res = app_args_parse(2, argv, &config);
    CHECK(res == 0);
    CHECK(config.is_fullscreen == false);
    CHECK(config.is_fullscreen_explicit == true);
  }

  SUBCASE("--ntsc") {
    char* argv[] = {(char*)"linapple", (char*)"--ntsc"};
    AppConfig_t config = {};
    int res = app_args_parse(2, argv, &config);
    CHECK(res == 0);
    CHECK(config.is_pal == false);
    CHECK(config.is_pal_explicit == true);
  }

  SUBCASE("--debugger") {
    char* argv[] = {(char*)"linapple", (char*)"--debugger"};
    AppConfig_t config = {};
    int res = app_args_parse(2, argv, &config);
    CHECK(res == 0);
    CHECK(config.disable_debugger == false);
    CHECK(config.disable_debugger_explicit == true);
  }
}
