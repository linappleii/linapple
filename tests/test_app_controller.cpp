// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <string>

#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
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

TEST_CASE("AppController: Drive 0 Remains Empty When No Disks Configured") {
  ScopedAppController_t controller_guard;
  TestFixtures::ScopedTempFile_t toml_file(".toml");
  {
    std::ofstream out(toml_file.path());
    out << "[Core]\n";
    out << "[Peripheral.DiskII]\n";
    out << "Drive1 = \"\"\n";
  }

  AppConfig_t config = {};
  app_config_default(&config);
  util_safe_strcpy(config.config_path.data(), toml_file.c_str(),
                   config.config_path.size());
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
}

TEST_CASE("AppController: Configured Disk Image Loaded at Startup") {
  ScopedAppController_t controller_guard;
  TestFixtures::ScopedTempFile_t conf_file(".conf");
  std::string master_path = Path::find_data_file("Master.dsk");
  {
    std::ofstream out(conf_file.path());
    out << "[Configuration]\n";
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

TEST_CASE("AppController: Modern TOML Initialization and Dispatch") {
  ScopedAppController_t controller_guard;
  TestFixtures::ScopedTempFile_t toml_file(".toml");
  TestFixtures::ScopedTempFile_t printer_out(".txt");
  {
    std::ofstream out(toml_file.path());
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
Filename = ")"
        << printer_out.path() << R"("
IdleLimit = 15
Append = true
)";
  }

  AppConfig_t config = {};
  app_config_default(&config);
  util_safe_strcpy(config.config_path.data(), toml_file.c_str(),
                   config.config_path.size());

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
}

TEST_CASE("AppController: Upgrade Config Diagnostic Handler") {
  TestFixtures::ScopedTempFile_t legacy_file(".conf");
  TestFixtures::ScopedTempFile_t modern_file(".toml");

  {
    std::ofstream out(legacy_file.path());
    out << "[Configuration]\nComputer Emulation = 2\n";
  }

  AppConfig_t config = {};
  app_config_default(&config);
  config.is_upgrade_config = true;
  config.intent = INTENT_DIAGNOSTIC;
  util_safe_strcpy(config.config_path.data(), legacy_file.c_str(),
                   config.config_path.size());
  util_safe_strcpy(config.upgrade_target_path.data(), modern_file.c_str(),
                   config.upgrade_target_path.size());

  bool handled = app_controller_handle_diagnostic_commands(&config);
  CHECK(handled == true);
  CHECK(access(modern_file.c_str(), R_OK) == 0);
}
