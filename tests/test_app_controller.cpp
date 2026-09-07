// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstring>
#include <string>

#include "AppConfig.h"
#include "Apple2Types.h"
#include "DiskCommands.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <unistd.h>

#include <fstream>

#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AppEnvironment.h"

TEST_CASE("AppController: Initialize and Shutdown") {
  AppConfig_t config = {};
  app_config_default(&config);

  app_env_resolve_paths(&config);
  // Test initialization
  int result = app_controller_initialize(&config);
  CHECK(result == 0);
  CHECK(g_state.mode == MODE_RUNNING);

  // Check if default directories are initialized
  CHECK(strlen(g_state.current_dir.data()) > 0);
  CHECK(strlen(g_state.hdd_dir.data()) > 0);
  CHECK(strlen(g_state.save_state_dir.data()) > 0);
  // Test shutdown
  app_controller_shutdown();
}

TEST_CASE("AppController: Video Mode Reset") {
  AppConfig_t config = {};
  app_config_default(&config);

  // 1. Init with PAL
  config.is_pal = true;
  app_controller_initialize(&config);
  CHECK(g_videotype == VT_COLOR_TVEMU);

  // 2. Re-init without PAL (should reset to standard)
  config.is_pal = false;
  app_controller_initialize(&config);
  CHECK(g_videotype == VT_COLOR_STANDARD);

  app_controller_shutdown();
}

TEST_CASE("AppController: Media Loading") {
  AppConfig_t config = {};
  app_config_default(&config);
  std::string disk_path = Path::find_data_file("Master.dsk");
  util_safe_strcpy(config.disk_path[0].data(), disk_path.c_str(), path_max_len);

  app_env_resolve_paths(&config);
  app_controller_initialize(&config);
  app_controller_load_initial_media(&config);

  // Explicitly think a bit to process commands from LoadInitialMedia
  for (int i = 0; i < 500; ++i) {
    peripheral_manager_think(100);
  }

  // Check if disk was loaded
  DiskStatus_t status = {};
  size_t status_size = sizeof(status);
  PeripheralStatus_t res = peripheral_query(
      disk_default_slot, disk_cmd_get_status, &status, &status_size);

  CHECK(res == peripheral_ok);
  CHECK(status.drive0_loaded == 1);

  app_controller_shutdown();
}

TEST_CASE("AppController: Diagnostic Commands") {
  AppConfig_t config = {};
  app_config_default(&config);
  config.intent = INTENT_HELP;

  // We don't want to actually print help to stdout during tests usually,
  // but here we just verify it returns true as expected.
  CHECK(app_controller_handle_diagnostic_commands(&config) == true);

  config.intent = INTENT_RUN;
  CHECK(app_controller_handle_diagnostic_commands(&config) == false);
}

TEST_CASE(
    "AppController: Computer Emulation and Screen Factor from Configuration") {
  const char* conf_path = "/tmp/test_custom_linapple.conf";
  {
    std::ofstream out(conf_path);
    out << "[Configuration]\n";
    out << "Computer Emulation = 1\n";
    out << "Screen factor = 2.0\n";
  }

  AppConfig_t config = {};
  app_config_default(&config);
  util_safe_strcpy(config.config_path.data(), conf_path, path_max_len);

  app_controller_initialize(&config);

  CHECK(g_apple2_type == A2TYPE_APPLE2PLUS);
  CHECK(g_state.screen_width == 1120);
  CHECK(g_state.screen_height == 768);

  app_controller_shutdown();
  unlink(conf_path);
}

TEST_CASE("AppController: Initialize Failure on Nonexistent ROM") {
  AppConfig_t config = {};
  app_config_default(&config);
  util_safe_strcpy(config.rom_path.data(), "/nonexistent/nope.rom",
                   config.rom_path.size());
  app_env_resolve_paths(&config);

  int result = app_controller_initialize(&config);
  CHECK(result != 0);

  app_controller_shutdown();
}

TEST_CASE("AppController: Drive 0 Remains Empty When No Disks Configured") {
  const char* toml_path = "/tmp/test_empty_app_controller.toml";
  {
    std::ofstream out(toml_path);
    out << "[Core]\n";
    out << "[Peripheral.DiskII]\n";
    out << "Drive1 = \"\"\n";
  }

  AppConfig_t config = {};
  app_config_default(&config);
  util_safe_strcpy(config.config_path.data(), toml_path, path_max_len);
  app_env_resolve_paths(&config);

  int result = app_controller_initialize(&config);
  CHECK(result == 0);

  for (int i = 0; i < 500; ++i) {
    peripheral_manager_think(100);
  }

  // Check that Drive 0 remains empty
  DiskStatus_t status = {};
  size_t status_size = sizeof(status);
  PeripheralStatus_t res = peripheral_query(
      disk_default_slot, disk_cmd_get_status, &status, &status_size);

  CHECK(res == peripheral_ok);
  CHECK(status.drive0_loaded == 0);

  app_controller_shutdown();
  unlink(toml_path);
}

TEST_CASE("AppController: Configured Disk Image Loaded at Startup") {
  const char* conf_path = "/tmp/test_autoload_linapple.conf";
  std::string master_path = Path::find_data_file("Master.dsk");
  {
    std::ofstream out(conf_path);
    out << "[Configuration]\n";
    out << "Disk Image 1 = " << master_path << "\n";
  }

  AppConfig_t config = {};
  app_config_default(&config);
  util_safe_strcpy(config.config_path.data(), conf_path, path_max_len);

  app_env_resolve_paths(&config);
  int result = app_controller_initialize(&config);
  CHECK(result == 0);

  for (int i = 0; i < 500; ++i) {
    peripheral_manager_think(100);
  }

  DiskStatus_t status = {};
  size_t status_size = sizeof(status);
  PeripheralStatus_t res = peripheral_query(
      disk_default_slot, disk_cmd_get_status, &status, &status_size);

  CHECK(res == peripheral_ok);
  CHECK(status.drive0_loaded == 1);

  app_controller_shutdown();
  unlink(conf_path);
}

TEST_CASE("AppController: Modern TOML Initialization and Dispatch") {
  const char* toml_path = "/tmp/test_modern_app_controller.toml";
  {
    std::ofstream out(toml_path);
    out << R"(
[Core]
Machine = "Apple //e"
EmulationSpeed = 2.0

[Video]
VideoStandard = "PAL"
VideoEmulation = "Color TV Emulation"
ScreenFactor = 1.5

[Slots]
Slot1 = "Parallel Printer"
Slot2 = "Super Serial Card"
Slot6 = "Disk II"

[Peripheral.ParallelPrinter]
Filename = "/tmp/test_printer_out.txt"
IdleLimit = 15
Append = true
)";
  }

  AppConfig_t config = {};
  app_config_default(&config);
  util_safe_strcpy(config.config_path.data(), toml_path, path_max_len);

  app_env_resolve_paths(&config);
  int result = app_controller_initialize(&config);
  CHECK(result == 0);

  CHECK(g_apple2_type == A2TYPE_APPLE2E);
  CHECK(g_videotype == VT_COLOR_TVEMU);
  CHECK(g_state.screen_width == 840);
  CHECK(g_state.screen_height == 576);

  // Check peripheral registered in Slot 1
  const Peripheral_t* slot1 = peripheral_get_registered(1);
  REQUIRE(slot1 != nullptr);
  CHECK(std::string(slot1->id) == "linapple.printer");

  app_controller_shutdown();
  unlink(toml_path);
}

TEST_CASE("AppController: Upgrade Config Diagnostic Handler") {
  const char* legacy_in = "/tmp/test_diag_upgrade.conf";
  const char* modern_out = "/tmp/test_diag_upgrade.toml";
  unlink(legacy_in);
  unlink(modern_out);

  {
    std::ofstream out(legacy_in);
    out << "[Configuration]\nComputer Emulation = 2\n";
  }

  AppConfig_t config = {};
  app_config_default(&config);
  config.is_upgrade_config = true;
  config.intent = INTENT_DIAGNOSTIC;
  util_safe_strcpy(config.config_path.data(), legacy_in, path_max_len);
  util_safe_strcpy(config.upgrade_target_path.data(), modern_out, path_max_len);

  bool handled = app_controller_handle_diagnostic_commands(&config);
  CHECK(handled == true);
  CHECK(access(modern_out, R_OK) == 0);

  unlink(legacy_in);
  unlink(modern_out);
}
