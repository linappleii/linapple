#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "frontends/common/AppArgs.h"
#include <cstring>

TEST_CASE("AppArgs: Basic Parsing") {
    char* argv[] = {(char*)"linapple", (char*)"--d1", (char*)"disk1.dsk", (char*)"--boot"};
    int argc = 4;
    AppConfig config = {};
    AppArgs_Parse(argc, argv, &config);

    CHECK(strcmp(config.szDiskPath[0].data(), "disk1.dsk") == 0);
    CHECK(config.bBoot == true);
    CHECK(config.intent == INTENT_RUN);
}

TEST_CASE("AppArgs: Diagnostic Intent") {
    char* argv[] = {(char*)"linapple", (char*)"--list-hardware"};
    int argc = 2;
    AppConfig config = {};
    AppArgs_Parse(argc, argv, &config);

    CHECK(config.bListHardware == true);
    CHECK(config.intent == INTENT_DIAGNOSTIC);
}

TEST_CASE("AppArgs: Frontend Pass-through") {
    // getopt_long might reorder argv, so we use a copy to be safe if we were to reuse it
    char* argv[] = {(char*)"linapple", (char*)"--boot", (char*)"--wayland", (char*)"pos1"};
    int argc = 4;
    AppConfig config = {};
    AppArgs_Parse(argc, argv, &config);

    CHECK(config.bBoot == true);
    CHECK(config.argc_extra == 2);

    bool found_wayland = false;
    bool found_pos1 = false;
    for (int i = 0; i < config.argc_extra; ++i) {
        if (strcmp(config.argv_extra[i], "--wayland") == 0) found_wayland = true;
        if (strcmp(config.argv_extra[i], "pos1") == 0) found_pos1 = true;
    }
    CHECK(found_wayland);
    CHECK(found_pos1);
}

TEST_CASE("AppArgs: Help Intent") {
    char* argv[] = {(char*)"linapple", (char*)"-h"};
    int argc = 2;
    AppConfig config = {};
    AppArgs_Parse(argc, argv, &config);

    CHECK(config.intent == INTENT_HELP);
}

TEST_CASE("AppArgs: Missing Argument error") {
    // --config requires an argument. Passing it as the last flag should trigger an error.
    char* argv[] = {(char*)"linapple", (char*)"--config"};
    int argc = 2;
    AppConfig config = {};
    int result = AppArgs_Parse(argc, argv, &config);

    CHECK(result != 0);
    CHECK(config.intent == INTENT_ERROR);
}
