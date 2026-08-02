#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "core/Util_Text.h"

TEST_CASE("AppConfig_t: Initialization") {
    AppConfig_t config = {};
    AppConfig_Default(&config);

    CHECK(config.intent == INTENT_RUN);
    CHECK(config.apple2Type == A2TYPE_APPLE2EENHANCED);
    CHECK(config.is_pal == false);
    CHECK(config.is_fullscreen == false);
    CHECK(config.is_list_hardware == false);
    CHECK(config.disk_path[0][0] == '\0');
    CHECK(config.disk_path[1][0] == '\0');
    CHECK(config.program_path[0] == '\0');
    CHECK(config.config_path[0] == '\0');
    CHECK(config.hardware_info_name[0] == '\0');
}

TEST_CASE("AppConfig_t: Manual Population") {
    AppConfig_t config = {};
    AppConfig_Default(&config);

    config.intent = INTENT_DIAGNOSTIC;
    Util_SafeStrCpy(config.disk_path[0].data(), "test.dsk", path_max_len);
    config.apple2Type = A2TYPE_APPLE2PLUS;
    config.is_pal = true;

    CHECK(config.intent == INTENT_DIAGNOSTIC);
    CHECK(strcmp(config.disk_path[0].data(), "test.dsk") == 0);
    CHECK(config.apple2Type == A2TYPE_APPLE2PLUS);
    CHECK(config.is_pal == true);
}
