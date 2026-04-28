#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "frontends/common/Util_Hash.h"
#include <string>

TEST_CASE("Util_Hash: MD5 correctness") {
    // Expected values generated with 'md5sum' CLI

    SUBCASE("Empty string") {
        CHECK(std::string(md5str("")) == "D41D8CD98F00B204E9800998ECF8427E");
    }

    SUBCASE("Basic string") {
        CHECK(std::string(md5str("linapple")) == "F3973CACD44E7688756F4956C2F591D2");
    }

    SUBCASE("Typical FTP URL") {
        const char* url = "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/";
        CHECK(std::string(md5str(url)) == "E5B4A34491470EE190E518FD4FE9C6DE");
    }

    SUBCASE("Long sentence") {
        const char* sentence = "The quick brown fox jumps over the lazy dog";
        CHECK(std::string(md5str(sentence)) == "9E107D9D372BB6826BD81D3542A419D6");
    }
}

TEST_CASE("Util_Hash: Consistency") {
    // Ensure that multiple calls with same input return the same value
    std::string first = md5str("consistency check");
    std::string second = md5str("consistency check");
    CHECK(first == second);
}
