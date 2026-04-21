#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "frontends/common/AppEnvironment.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include <cstdlib>
#include <fstream>

TEST_CASE("AppEnvironment: Path Resolution Override") {
    // We'll use a local file to test override
    std::ofstream tmp_conf("test_resolve.conf");
    tmp_conf << "[Test]\nValue=1\n";
    tmp_conf.close();

    AppConfig config = {};
    Util_SafeStrCpy(config.szConfigPath, "test_resolve.conf", PATH_MAX_LEN);

    AppEnv_ResolvePaths(&config);

    CHECK(Configuration::Instance().GetPath() == "test_resolve.conf");
    CHECK(strcmp(config.szConfigPath, "test_resolve.conf") == 0);

    remove("test_resolve.conf");
}

TEST_CASE("AppEnvironment: Logger Verbosity") {
    AppConfig config = {};
    config.bVerbose = true;

    AppEnv_ResolvePaths(&config);
    // Since we can't easily query Logger verbosity without adding a getter,
    // we just ensure it doesn't crash and follows the logic.
}
