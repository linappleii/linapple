// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppEnvironment.h"
#include "test_fixtures.h"

namespace {

struct ScopedConfigPathReset_t {
  std::string original_path_{Configuration_t::instance().get_path()};

  ScopedConfigPathReset_t() = default;
  ~ScopedConfigPathReset_t() {
    Configuration_t::instance().set_path(original_path_);
  }

  ScopedConfigPathReset_t(const ScopedConfigPathReset_t&) = delete;
  auto operator=(const ScopedConfigPathReset_t&)
      -> ScopedConfigPathReset_t& = delete;
  ScopedConfigPathReset_t(ScopedConfigPathReset_t&&) = delete;
  auto operator=(ScopedConfigPathReset_t&&)
      -> ScopedConfigPathReset_t& = delete;
};

struct ScopedLoggerReset_t {
  LogLevel_t original_verbosity_{Logger::get_verbosity()};

  ScopedLoggerReset_t() = default;
  ~ScopedLoggerReset_t() {
    Logger::set_callback(nullptr);
    Logger::set_verbosity(original_verbosity_);
  }

  ScopedLoggerReset_t(const ScopedLoggerReset_t&) = delete;
  auto operator=(const ScopedLoggerReset_t&) -> ScopedLoggerReset_t& = delete;
  ScopedLoggerReset_t(ScopedLoggerReset_t&&) = delete;
  auto operator=(ScopedLoggerReset_t&&) -> ScopedLoggerReset_t& = delete;
};

using TestFixtures::ScopedEnvVar_t;

LogLevel_t g_last_log_level = LogLevel_t::k_silent;
int g_log_callback_count = 0;

auto test_log_callback(LogLevel_t level, const char* /*message*/) -> void {
  g_last_log_level = level;
  ++g_log_callback_count;
}

}  // namespace

TEST_CASE("AppEnvironment: Path Resolution Override") {
  ScopedConfigPathReset_t config_path_guard;

  SUBCASE("Explicit configuration file override is honored") {
    TestFixtures::ScopedTempFile_t tmp_conf(".conf");
    {
      std::ofstream out(tmp_conf.path());
      out << "[Test]\nvalue=1\n";
    }

    AppConfig_t config = {};
    util_safe_strcpy(config.config_path.data(), tmp_conf.c_str(),
                     config.config_path.size());

    app_env_resolve_paths(&config);

    CHECK(Configuration_t::instance().get_path() == tmp_conf.path());
    CHECK(std::string(config.config_path.data()) == tmp_conf.path());
  }

  SUBCASE("Nullptr config pointer is handled safely") {
    const std::string before = Configuration_t::instance().get_path();
    app_env_resolve_paths(nullptr);
    CHECK(Configuration_t::instance().get_path() == before);
  }
}

TEST_CASE("AppEnvironment: Logger Verbosity") {
  ScopedLoggerReset_t logger_reset;
  Logger::set_callback(test_log_callback);

  SUBCASE("Verbose mode sets k_perf verbosity and delivers perf logs") {
    AppConfig_t config = {};
    config.is_verbose = true;

    app_env_resolve_paths(&config);

    CHECK(Logger::get_verbosity() == LogLevel_t::k_perf);

    g_log_callback_count = 0;
    g_last_log_level = LogLevel_t::k_silent;
    Logger::perf("test perf\n");
    CHECK(g_log_callback_count == 1);
    CHECK(g_last_log_level == LogLevel_t::k_perf);
  }

  SUBCASE("Logging mode sets k_info verbosity and filters perf logs") {
    AppConfig_t config = {};
    config.is_verbose = false;
    config.is_log = true;

    app_env_resolve_paths(&config);

    CHECK(Logger::get_verbosity() == LogLevel_t::k_info);

    g_log_callback_count = 0;
    g_last_log_level = LogLevel_t::k_silent;
    Logger::perf("test perf\n");
    CHECK(g_log_callback_count == 0);
    CHECK(g_last_log_level == LogLevel_t::k_silent);

    Logger::info("test info\n");
    CHECK(g_log_callback_count == 1);
    CHECK(g_last_log_level == LogLevel_t::k_info);
  }

  SUBCASE("Default mode sets k_warning verbosity and filters info logs") {
    AppConfig_t config = {};
    config.is_verbose = false;
    config.is_log = false;

    app_env_resolve_paths(&config);

    CHECK(Logger::get_verbosity() == LogLevel_t::k_warning);

    g_log_callback_count = 0;
    g_last_log_level = LogLevel_t::k_silent;
    Logger::info("test info\n");
    CHECK(g_log_callback_count == 0);
    CHECK(g_last_log_level == LogLevel_t::k_silent);

    Logger::warning("test warning\n");
    CHECK(g_log_callback_count == 1);
    CHECK(g_last_log_level == LogLevel_t::k_warning);
  }
}

TEST_CASE("AppEnvironment: XDG Config Dirs Data Paths") {
  SUBCASE("Default XDG config fallback when XDG_CONFIG_DIRS is unset") {
    ScopedEnvVar_t env_guard("XDG_CONFIG_DIRS", nullptr);
    const auto paths = Path::get_data_search_paths();
    CHECK(std::find(paths.begin(), paths.end(), "/etc/xdg/linapple/") !=
          paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "/etc/linapple/") !=
          paths.end());
  }

  SUBCASE("Custom XDG_CONFIG_DIRS paths are included in search paths") {
    ScopedEnvVar_t env_guard("XDG_CONFIG_DIRS", "/custom/share:/other/dir");
    const auto paths = Path::get_data_search_paths();
    CHECK(std::find(paths.begin(), paths.end(), "/custom/share/linapple/") !=
          paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "/other/dir/linapple/") !=
          paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "/etc/linapple/") !=
          paths.end());
  }
}

TEST_CASE("AppEnvironment: Three-Tier Precedence (CLI > Config > Default)") {
  ScopedConfigPathReset_t config_reset;

  SUBCASE("Tier 3 Default: When neither CLI nor config provides a setting") {
    ScopedEnvVar_t xdg_guard("XDG_CONFIG_HOME", "/nonexistent");
    ScopedEnvVar_t home_guard("HOME", "/nonexistent");
    ScopedEnvVar_t xdg_dirs_guard("XDG_CONFIG_DIRS", "/nonexistent");

    AppConfig_t config = {};
    app_env_resolve_paths(&config);

    // BootOnStartup schema default is true
    CHECK(config.is_boot == true);
    // Fullscreen schema default is false
    CHECK(config.is_fullscreen == false);
    // VideoStandard schema default is NTSC (not PAL)
    CHECK(config.is_pal == false);
    // EnableDebugger schema default is true (disable_debugger == false)
    CHECK(config.disable_debugger == false);
  }

  SUBCASE(
      "Tier 2 Config Value: Config file overrides defaults when CLI switch is "
      "absent") {
    TestFixtures::ScopedTempFile_t custom_conf(".toml");
    std::ofstream out(custom_conf.path());
    out << "[Core]\n";
    out << "BootOnStartup = false\n";
    out << "EnableDebugger = false\n";
    out << "[Video]\n";
    out << "VideoStandard = \"PAL\"\n";
    out << "Fullscreen = true\n";
    out.close();

    AppConfig_t config = {};
    util_safe_strcpy(config.config_path.data(), custom_conf.path().c_str(),
                     config.config_path.size());
    app_env_resolve_paths(&config);

    CHECK(config.is_boot == false);
    CHECK(config.disable_debugger == true);
    CHECK(config.is_pal == true);
    CHECK(config.is_fullscreen == true);
  }

  SUBCASE(
      "Tier 1 CLI Switch: Explicit CLI switch overrides config file values") {
    TestFixtures::ScopedTempFile_t custom_conf(".toml");
    std::ofstream out(custom_conf.path());
    out << "[Core]\n";
    out << "BootOnStartup = false\n";
    out << "EnableDebugger = false\n";
    out << "[Video]\n";
    out << "VideoStandard = \"PAL\"\n";
    out << "Fullscreen = true\n";
    out.close();

    AppConfig_t config = {};
    util_safe_strcpy(config.config_path.data(), custom_conf.path().c_str(),
                     config.config_path.size());

    // Explicit CLI switches overriding config.toml
    config.is_boot = true;
    config.is_boot_explicit = true;
    config.disable_debugger = false;
    config.disable_debugger_explicit = true;
    config.is_pal = false;
    config.is_pal_explicit = true;
    config.is_fullscreen = false;
    config.is_fullscreen_explicit = true;

    app_env_resolve_paths(&config);

    CHECK(config.is_boot == true);
    CHECK(config.disable_debugger == false);
    CHECK(config.is_pal == false);
    CHECK(config.is_fullscreen == false);
  }
}
