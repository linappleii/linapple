// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <fstream>
#include <string>

#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral_Types.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AppEnvironment.h"
#include "test_fixtures.h"

namespace {

struct ScopedAppController_t {
  ScopedAppController_t() = default;
  ~ScopedAppController_t() { app_controller_shutdown(); }
  ScopedAppController_t(const ScopedAppController_t&) = delete;
  auto operator=(const ScopedAppController_t&)
      -> ScopedAppController_t& = delete;
  ScopedAppController_t(ScopedAppController_t&&) = delete;
  auto operator=(ScopedAppController_t&&) -> ScopedAppController_t& = delete;
};

auto is_valid_directory(const char* path) -> bool {
  if (path == nullptr || *path == '\0') {
    return false;
  }
  struct stat st{};
  if (stat(path, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode) && (access(path, F_OK | R_OK) == 0);
}

}  // namespace

TEST_CASE("AppController: Initialize and Shutdown") {
  ScopedAppController_t controller_guard;
  AppConfig_t config = {};
  app_config_default(&config);

  app_env_resolve_paths(&config);
  // Test initialization
  int result = app_controller_initialize(&config);
  CHECK(result == 0);
  CHECK(g_state.mode == MODE_RUNNING);

  // Check if default directories are initialized, valid, and accessible
  CHECK(g_state.current_dir.at(0) != '\0');
  CHECK(g_state.hdd_dir.at(0) != '\0');
  CHECK(g_state.save_state_dir.at(0) != '\0');
  CHECK(is_valid_directory(g_state.current_dir.data()));
  CHECK(is_valid_directory(g_state.hdd_dir.data()));
  CHECK(is_valid_directory(g_state.save_state_dir.data()));

  std::string expected_user_dir = Path::get_user_data_dir();
  while (expected_user_dir.size() > 1 && expected_user_dir.back() == '/') {
    expected_user_dir.pop_back();
  }
  CHECK(std::string(g_state.current_dir.data()) == expected_user_dir);
  CHECK(std::string(g_state.hdd_dir.data()) == expected_user_dir);
  CHECK(std::string(g_state.save_state_dir.data()) == expected_user_dir);
}

TEST_CASE("AppController: Video Mode Reset") {
  ScopedAppController_t controller_guard;
  AppConfig_t config = {};
  app_config_default(&config);

  // 1. Init with PAL
  config.is_pal = true;
  int result_pal = app_controller_initialize(&config);
  CHECK(result_pal == 0);
  CHECK(g_videotype == VT_COLOR_TVEMU);

  // 2. Re-init without PAL (should reset to standard)
  config.is_pal = false;
  int result_std = app_controller_initialize(&config);
  CHECK(result_std == 0);
  CHECK(g_videotype == VT_COLOR_STANDARD);
}

TEST_CASE("AppController: Media Loading") {
  ScopedAppController_t controller_guard;
  AppConfig_t config = {};
  app_config_default(&config);
  std::string disk_path = Path::find_data_file("Master.dsk");
  util_safe_strcpy(config.disk_path[0].data(), disk_path.c_str(),
                   config.disk_path[0].size());

  app_env_resolve_paths(&config);
  int result = app_controller_initialize(&config);
  CHECK(result == 0);
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
  ScopedAppController_t controller_guard;
  TestFixtures::ScopedTempFile_t conf_file(".conf");
  {
    std::ofstream out(conf_file.path());
    out << "[Configuration]\n";
    out << "Computer Emulation = 1\n";
    out << "Screen factor = 2.0\n";
  }

  AppConfig_t config = {};
  app_config_default(&config);
  util_safe_strcpy(config.config_path.data(), conf_file.c_str(),
                   config.config_path.size());

  int result = app_controller_initialize(&config);
  CHECK(result == 0);

  CHECK(g_apple2_type == A2TYPE_APPLE2PLUS);
  CHECK(g_state.screen_width == 1120);
  CHECK(g_state.screen_height == 768);
}

TEST_CASE("AppController: Initialize Failure on Nonexistent ROM") {
  ScopedAppController_t controller_guard;
  AppConfig_t config = {};
  app_config_default(&config);
  util_safe_strcpy(config.rom_path.data(), "/nonexistent/nope.rom",
                   config.rom_path.size());
  app_env_resolve_paths(&config);

  int result = app_controller_initialize(&config);
  CHECK(result != 0);
}

TEST_CASE("AppController: Slot 6 Autoload Fallback to Master.dsk") {
  ScopedAppController_t controller_guard;
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
}

TEST_CASE("AppController: Slot 6 Autoload Enabled with Configured Image") {
  ScopedAppController_t controller_guard;
  TestFixtures::ScopedTempFile_t conf_file(".conf");
  std::string master_path = Path::find_data_file("Master.dsk");
  {
    std::ofstream out(conf_file.path());
    out << "[Configuration]\n";
    out << "Slot 6 Autoload = 1\n";
    out << "Disk Image 1 = " << master_path << "\n";
  }

  AppConfig_t config = {};
  app_config_default(&config);
  util_safe_strcpy(config.config_path.data(), conf_file.c_str(),
                   config.config_path.size());

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
}
