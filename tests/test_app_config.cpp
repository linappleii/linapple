#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "core/Util_Text.h"

TEST_CASE("AppConfig: Initialization") {
    AppConfig config = {};
    AppConfig_Default(&config);

    CHECK(config.intent == INTENT_RUN);
    CHECK(config.apple2Type == A2TYPE_APPLE2EENHANCED);
    CHECK(config.bPAL == false);
    CHECK(config.bFullscreen == false);
    CHECK(config.bListHardware == false);
    CHECK(config.szDiskPath[0][0] == '\0');
    CHECK(config.szDiskPath[1][0] == '\0');
    CHECK(config.szProgramPath[0] == '\0');
    CHECK(config.szConfigPath[0] == '\0');
    CHECK(config.szHardwareInfoName[0] == '\0');
}

TEST_CASE("AppConfig: Manual Population") {
    AppConfig config = {};
    AppConfig_Default(&config);

    config.intent = INTENT_DIAGNOSTIC;
    Util_SafeStrCpy(config.szDiskPath[0].data(), "test.dsk", path_max_len);
    config.apple2Type = A2TYPE_APPLE2PLUS;
    config.bPAL = true;

    CHECK(config.intent == INTENT_DIAGNOSTIC);
    CHECK(strcmp(config.szDiskPath[0].data(), "test.dsk") == 0);
    CHECK(config.apple2Type == A2TYPE_APPLE2PLUS);
    CHECK(config.bPAL == true);
}
