// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <stdlib.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppEnvironment.h"
#include "test_fixtures.h"

namespace {

struct ScopedConfigPathReset {
  std::string original_path_{Configuration::instance().get_path()};

  ScopedConfigPathReset() = default;
  ~ScopedConfigPathReset() {
    Configuration::instance().set_path(original_path_);
  }

  ScopedConfigPathReset(const ScopedConfigPathReset&) = delete;
  auto operator=(const ScopedConfigPathReset&)
      -> ScopedConfigPathReset& = delete;
  ScopedConfigPathReset(ScopedConfigPathReset&&) = delete;
  auto operator=(ScopedConfigPathReset&&)
      -> ScopedConfigPathReset& = delete;
};

struct ScopedLoggerReset {
  LogLevel original_verbosity_{Logger::get_verbosity()};

  ScopedLoggerReset() = default;
  ~ScopedLoggerReset() {
    Logger::set_callback(nullptr);
    Logger::set_verbosity(original_verbosity_);
  }

  ScopedLoggerReset(const ScopedLoggerReset&) = delete;
  auto operator=(const ScopedLoggerReset&) -> ScopedLoggerReset& = delete;
  ScopedLoggerReset(ScopedLoggerReset&&) = delete;
  auto operator=(ScopedLoggerReset&&) -> ScopedLoggerReset& = delete;
};

class ScopedEnvVar {
 private:
  std::string name_;
  std::string prev_value_;
  bool had_value_{false};

 public:
  explicit ScopedEnvVar(std::string name, const char* new_value)
      : name_(std::move(name)) {
    const char* prev = std::getenv(name_.c_str());
    if (prev != nullptr) {
      prev_value_ = prev;
      had_value_ = true;
    }
    if (new_value != nullptr) {
      setenv(name_.c_str(), new_value, 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  ~ScopedEnvVar() {
    if (had_value_) {
      setenv(name_.c_str(), prev_value_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  ScopedEnvVar(const ScopedEnvVar&) = delete;
  auto operator=(const ScopedEnvVar&) -> ScopedEnvVar& = delete;
  ScopedEnvVar(ScopedEnvVar&&) = delete;
  auto operator=(ScopedEnvVar&&) -> ScopedEnvVar& = delete;
};

LogLevel last_log_level = LogLevel::silent;
int log_callback_count = 0;

auto test_log_callback(LogLevel level, const char* /*message*/) -> void {
  last_log_level = level;
  ++log_callback_count;
}

}  // namespace

TEST_CASE("AppEnvironment: Path Resolution Override") {
  ScopedConfigPathReset config_path_guard;

  SUBCASE("Explicit configuration file override is honored") {
    TestFixtures::ScopedTempFile tmp_conf(".conf");
    {
      std::ofstream out(tmp_conf.path());
      out << "[Test]\nvalue=1\n";
    }

    AppConfig config = {};
    util_safe_strcpy(config.config_path.data(), tmp_conf.c_str(),
                     config.config_path.size());

    app_env_resolve_paths(&config);

    CHECK(Configuration::instance().get_path() == tmp_conf.path());
    CHECK(std::string(config.config_path.data()) == tmp_conf.path());
  }

  SUBCASE("Nullptr config pointer is handled safely") {
    const std::string before = Configuration::instance().get_path();
    app_env_resolve_paths(nullptr);
    CHECK(Configuration::instance().get_path() == before);
  }
}

TEST_CASE("AppEnvironment: Logger Verbosity") {
  ScopedLoggerReset logger_reset;
  Logger::set_callback(test_log_callback);

  SUBCASE("Verbose mode sets perf verbosity and delivers perf logs") {
    AppConfig config = {};
    config.is_verbose = true;

    app_env_resolve_paths(&config);

    CHECK(Logger::get_verbosity() == LogLevel::perf);

    log_callback_count = 0;
    last_log_level = LogLevel::silent;
    Logger::perf("test perf\n");
    CHECK(log_callback_count == 1);
    CHECK(last_log_level == LogLevel::perf);
  }

  SUBCASE("Logging mode sets info verbosity and filters perf logs") {
    AppConfig config = {};
    config.is_verbose = false;
    config.is_log = true;

    app_env_resolve_paths(&config);

    CHECK(Logger::get_verbosity() == LogLevel::info);

    log_callback_count = 0;
    last_log_level = LogLevel::silent;
    Logger::perf("test perf\n");
    CHECK(log_callback_count == 0);
    CHECK(last_log_level == LogLevel::silent);

    Logger::info("test info\n");
    CHECK(log_callback_count == 1);
    CHECK(last_log_level == LogLevel::info);
  }

  SUBCASE("Default mode sets warning verbosity and filters info logs") {
    AppConfig config = {};
    config.is_verbose = false;
    config.is_log = false;

    app_env_resolve_paths(&config);

    CHECK(Logger::get_verbosity() == LogLevel::warning);

    log_callback_count = 0;
    last_log_level = LogLevel::silent;
    Logger::info("test info\n");
    CHECK(log_callback_count == 0);
    CHECK(last_log_level == LogLevel::silent);

    Logger::warning("test warning\n");
    CHECK(log_callback_count == 1);
    CHECK(last_log_level == LogLevel::warning);
  }
}

TEST_CASE("AppEnvironment: XDG Config Dirs Data Paths") {
  SUBCASE("Default XDG config fallback when XDG_CONFIG_DIRS is unset") {
    ScopedEnvVar env_guard("XDG_CONFIG_DIRS", nullptr);
    const auto paths = Path::get_data_search_paths();
    CHECK(std::find(paths.begin(), paths.end(), "/etc/xdg/linapple/") !=
          paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "/etc/linapple/") !=
          paths.end());
  }

  SUBCASE("Custom XDG_CONFIG_DIRS paths are included in search paths") {
    ScopedEnvVar env_guard("XDG_CONFIG_DIRS", "/custom/share:/other/dir");
    const auto paths = Path::get_data_search_paths();
    CHECK(std::find(paths.begin(), paths.end(), "/custom/share/linapple/") !=
          paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "/other/dir/linapple/") !=
          paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "/etc/linapple/") !=
          paths.end());
  }
}
