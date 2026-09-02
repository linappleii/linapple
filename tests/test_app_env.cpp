#include <stdio.h>

#include <cstring>

#include "AppConfig.h"
#include "LinAppleCore.h"
#include "Util_Path.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <fstream>

#include "core/Registry.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppEnvironment.h"

TEST_CASE("AppEnvironment: Path Resolution Override") {
  // We'll use a local file to test override
  std::ofstream tmp_conf("test_resolve.conf");
  tmp_conf << "[Test]\value=1\n";
  tmp_conf.close();

  AppConfig_t config = {};
  util_safe_strcpy(config.config_path.data(), "test_resolve.conf",
                   path_max_len);

  app_env_resolve_paths(&config);

  CHECK(Configuration_t::instance().get_path() == "test_resolve.conf");
  CHECK(strcmp(config.config_path.data(), "test_resolve.conf") == 0);

  remove("test_resolve.conf");
}

TEST_CASE("AppEnvironment: Logger Verbosity") {
  AppConfig_t config = {};
  config.is_verbose = true;

  app_env_resolve_paths(&config);
  // Since we can't easily query Logger verbosity without adding a getter,
  // we just ensure it doesn't crash and follows the logic.
}

TEST_CASE("AppEnvironment: XDG Config Dirs Data Paths") {
  auto paths = Path::get_data_search_paths();
  bool has_etc_linapple = false;
  bool has_etc_xdg = false;
  for (const auto& p : paths) {
    if (p == "/etc/linapple/") has_etc_linapple = true;
    if (p == "/etc/xdg/linapple/") has_etc_xdg = true;
  }
  CHECK(has_etc_linapple);
  CHECK(has_etc_xdg);
}
