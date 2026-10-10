// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"
#include "frontends/common/Util_Hash.h"

TEST_CASE("Util_Hash: MD5 correctness") {
  SUBCASE("Null input") { CHECK(md5str(nullptr).empty()); }

  SUBCASE("Empty string") {
    CHECK(md5str("") == "D41D8CD98F00B204E9800998ECF8427E");
  }

  SUBCASE("Basic string") {
    CHECK(md5str("linapple") == "F3973CACD44E7688756F4956C2F591D2");
  }

  SUBCASE("Typical FTP URL") {
    const char* url = "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/";
    CHECK(md5str(url) == "E5B4A34491470EE190E518FD4FE9C6DE");
  }

  SUBCASE("Long sentence") {
    const char* sentence = "The quick brown fox jumps over the lazy dog";
    CHECK(md5str(sentence) == "9E107D9D372BB6826BD81D3542A419D6");
  }
}

TEST_CASE("Util_Hash: Consistency") {
  std::string first = md5str("consistency check");
  std::string second = md5str("consistency check");
  CHECK(first == second);
}

TEST_CASE("Util_Hash: Concurrency and Thread Safety") {
  constexpr size_t num_threads = 8;
  std::vector<std::thread> threads;
  threads.reserve(num_threads);
  std::vector<std::string> results(num_threads);

  for (size_t t = 0; t < num_threads; ++t) {
    threads.emplace_back([t, &results]() -> void {
      results.at(t) = md5str(t % 2 == 0 ? "linapple" : "consistency check");
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  for (size_t t = 0; t < num_threads; ++t) {
    if (t % 2 == 0) {
      CHECK(results.at(t) == "F3973CACD44E7688756F4956C2F591D2");
    } else {
      CHECK(results.at(t) == md5str("consistency check"));
    }
  }
}
