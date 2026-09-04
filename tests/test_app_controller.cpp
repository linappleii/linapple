// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstring>
#include <string>

#include "AppConfig.h"
#include "Apple2Types.h"
#include "DiskCommands.h"
#include "Peripheral_Types.h"
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

TEST_CASE("AppController: Slot 6 Autoload Fallback to Master.dsk") {
  AppConfig_t config = {};
  app_config_default(&config);
  app_env_resolve_paths(&config);

  int result = app_controller_initialize(&config);
  CHECK(result == 0);

  for (int i = 0; i < 500; ++i) {
    peripheral_manager_think(100);
  }

  // Check if Master.dsk was automatically inserted into drive 0
  DiskStatus_t status = {};
  size_t status_size = sizeof(status);
  PeripheralStatus_t res = peripheral_query(
      disk_default_slot, disk_cmd_get_status, &status, &status_size);

  CHECK(res == peripheral_ok);
  CHECK(status.drive0_loaded == 1);

  std::string disk1_path =
      Configuration_t::instance().get_string("Slots", REGVALUE_DISK_IMAGE1);
  CHECK(disk1_path.find("Master.dsk") != std::string::npos);

  app_controller_shutdown();
}

TEST_CASE("AppController: Slot 6 Autoload Enabled with Configured Image") {
  const char* conf_path = "/tmp/test_autoload_linapple.conf";
  std::string master_path = Path::find_data_file("Master.dsk");
  {
    std::ofstream out(conf_path);
    out << "[Configuration]\n";
    out << "Slot 6 Autoload = 1\n";
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
