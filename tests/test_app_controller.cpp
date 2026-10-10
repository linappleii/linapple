// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <string>

#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AppEnvironment.h"
#include "test_fixtures.h"
#ifndef ENABLE_PERIPHERAL_HARDDISK
#include "test_fixtures_core.h"
#endif

namespace {

using TestConfig = TestFixtures::ScopedTestConfig;

// Every case here builds a real core, so every case states the machine it
// wants. Left undeclared, the slots come from the fallbacks in
// peripheral_register_internal -- a printer, a Super Serial Card, a
// Mockingboard and a Disk II that no case here exercises.
auto declare(const TestConfig& machine, AppConfig* config) -> void {
  util_safe_strcpy(config->config_path.data(), machine.c_str(),
                   config->config_path.size());
}

struct ScopedAppController {
  ScopedAppController() = default;
  ~ScopedAppController() { app_controller_shutdown(); }
  ScopedAppController(const ScopedAppController&) = delete;
  auto operator=(const ScopedAppController&)
      -> ScopedAppController& = delete;
  ScopedAppController(ScopedAppController&&) = delete;
  auto operator=(ScopedAppController&&) -> ScopedAppController& = delete;
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
  ScopedAppController controller_guard;
  TestConfig machine(TestConfig::enhanced_2e_only());
  AppConfig config = {};
  app_config_default(&config);
  declare(machine, &config);

  app_env_resolve_paths(&config);
  // Test initialization
  int result = app_controller_initialize(&config);
  CHECK(result == 0);
  CHECK(system_state.mode == app_mode_running);

  // Check if default directories are initialized, valid, and accessible
  CHECK(system_state.current_dir.at(0) != '\0');
  CHECK(system_state.hdd_dir.at(0) != '\0');
  CHECK(system_state.save_state_dir.at(0) != '\0');
  CHECK(is_valid_directory(system_state.current_dir.data()));
  CHECK(is_valid_directory(system_state.hdd_dir.data()));
  CHECK(is_valid_directory(system_state.save_state_dir.data()));

  std::string expected_user_dir = Path::get_user_data_dir();
  while (expected_user_dir.size() > 1 && expected_user_dir.back() == '/') {
    expected_user_dir.pop_back();
  }
  CHECK(std::string(system_state.current_dir.data()) == expected_user_dir);
  CHECK(std::string(system_state.hdd_dir.data()) == expected_user_dir);
  CHECK(std::string(system_state.save_state_dir.data()) == expected_user_dir);
}

TEST_CASE("AppController: Video Mode Reset") {
  ScopedAppController controller_guard;
  TestConfig machine(TestConfig::enhanced_2e_only());
  AppConfig config = {};
  app_config_default(&config);
  declare(machine, &config);

  // 1. Init with PAL
  config.is_pal = true;
  int result_pal = app_controller_initialize(&config);
  CHECK(result_pal == 0);
  CHECK(videotype == VT_COLOR_TVEMU);

  // 2. Re-init without PAL (should reset to standard)
  config.is_pal = false;
  int result_std = app_controller_initialize(&config);
  CHECK(result_std == 0);
  CHECK(videotype == VT_COLOR_STANDARD);
}

#ifdef ENABLE_PERIPHERAL_DISK
TEST_CASE("AppController: Media Loading") {
  ScopedAppController controller_guard;
  TestConfig machine(TestConfig::disk_ii_only());
  AppConfig config = {};
  app_config_default(&config);
  declare(machine, &config);
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
  DiskStatus status = {};
  size_t status_size = sizeof(status);
  PeripheralStatus res = peripheral_query(
      disk_default_slot, disk_query_status, &status, &status_size);

  CHECK(res == peripheral_ok);
  CHECK(status.drive0_loaded == 1);
}
#endif

TEST_CASE("AppController: Diagnostic Commands") {
  AppConfig config = {};
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
  ScopedAppController controller_guard;
  TestConfig::Description description;
  description.machine_type = TestConfig::machine_apple2_plus;
  description.extras.push_back({"Configuration", "Screen factor", "2.0"});
  TestConfig machine(description);

  AppConfig config = {};
  app_config_default(&config);
  declare(machine, &config);

  int result = app_controller_initialize(&config);
  CHECK(result == 0);

  CHECK(current_apple2_type == A2TYPE_APPLE2PLUS);
  CHECK(system_state.screen_width == 1120);
  CHECK(system_state.screen_height == 768);
}

TEST_CASE("AppController: Initialize Failure on Nonexistent ROM") {
  ScopedAppController controller_guard;
  TestConfig machine(TestConfig::enhanced_2e_only());
  AppConfig config = {};
  app_config_default(&config);
  declare(machine, &config);
  util_safe_strcpy(config.rom_path.data(), "/nonexistent/nope.rom",
                   config.rom_path.size());
  app_env_resolve_paths(&config);

  int result = app_controller_initialize(&config);
  CHECK(result != 0);
}

#ifdef ENABLE_PERIPHERAL_DISK
TEST_CASE("AppController: Slot 6 Autoload Fallback to Master.dsk") {
  ScopedAppController controller_guard;
  TestConfig machine(TestConfig::disk_ii_only());
  AppConfig config = {};
  app_config_default(&config);
  declare(machine, &config);
  app_env_resolve_paths(&config);

  int result = app_controller_initialize(&config);
  CHECK(result == 0);

  for (int i = 0; i < 500; ++i) {
    peripheral_manager_think(100);
  }

  // Check if Master.dsk was automatically inserted into drive 0
  DiskStatus status = {};
  size_t status_size = sizeof(status);
  PeripheralStatus res = peripheral_query(
      disk_default_slot, disk_query_status, &status, &status_size);

  CHECK(res == peripheral_ok);
  CHECK(status.drive0_loaded == 1);

  std::string disk1_path =
      Configuration::instance().get_string("Slots", cfg_disk_image1);
  CHECK(disk1_path.find("Master.dsk") != std::string::npos);
}

TEST_CASE("AppController: Slot 6 Autoload Enabled with Configured Image") {
  ScopedAppController controller_guard;
  std::string master_path = Path::find_data_file("Master.dsk");
  TestConfig::Description description(TestConfig::disk_ii_only());
  description.extras.push_back({"Configuration", "Slot 6 Autoload", "1"});
  description.extras.push_back({"Configuration", "Disk Image 1", master_path});
  TestConfig machine(description);

  AppConfig config = {};
  app_config_default(&config);
  declare(machine, &config);

  app_env_resolve_paths(&config);
  int result = app_controller_initialize(&config);
  CHECK(result == 0);

  for (int i = 0; i < 500; ++i) {
    peripheral_manager_think(100);
  }

  DiskStatus status = {};
  size_t status_size = sizeof(status);
  PeripheralStatus res = peripheral_query(
      disk_default_slot, disk_query_status, &status, &status_size);

  CHECK(res == peripheral_ok);
  CHECK(status.drive0_loaded == 1);
}
#endif

TEST_CASE("AppController: FTP Configuration Defaults and Preferences") {
  ScopedAppController controller_guard;
  TestConfig::Description description(TestConfig::disk_ii_only());
  description.extras.push_back(
      {"Preferences", "FTP Server", "ftp://test.server/games/"});
  description.extras.push_back(
      {"Preferences", "FTP ServerHDD", "ftp://test.server/hdd/"});
  description.extras.push_back({"Preferences", "FTP UserPass", "user:pass"});
  TestConfig machine(description);

  AppConfig config = {};
  app_config_default(&config);
  declare(machine, &config);

  app_env_resolve_paths(&config);
  int result = app_controller_initialize(&config);
  CHECK(result == 0);

  CHECK(std::string(system_state.ftp_server.data()) ==
        "ftp://test.server/games/");
  CHECK(std::string(system_state.ftp_server_hdd.data()) ==
        "ftp://test.server/hdd/");
  CHECK(std::string(system_state.ftp_user_pass.data()) == "user:pass");
  CHECK(is_valid_directory(system_state.ftp_local_dir.data()));
}

#ifndef ENABLE_PERIPHERAL_HARDDISK
TEST_CASE(
    "AppController: --hd1 in a build without the hard disk says so once and "
    "the machine runs on") {
  ScopedAppController controller_guard;
  TestConfig machine(TestConfig::enhanced_2e_only());
  AppConfig config = {};
  app_config_default(&config);
  declare(machine, &config);
  util_safe_strcpy(config.harddisk_path.at(0).data(), "image.hdv",
                   config.harddisk_path.at(0).size());
  config.harddisk_path_from_args.at(0) = true;

  REQUIRE(app_controller_initialize(&config) == 0);
  TestFixtures::ScopedLogCapture log;
  app_controller_load_initial_media(&config);

  CHECK(log.count_containing(
            "--hd1: this build has no Harddisk card; image not mounted") == 1);
  CHECK(system_state.mode == app_mode_running);
}
#endif
