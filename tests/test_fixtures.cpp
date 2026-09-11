// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "test_fixtures.h"

#include <unistd.h>

#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include "doctest.h"

using TestFixtures::EphemeralDiskFixture_t;

TEST_CASE("EphemeralDiskFixture: Lifecycle and Isolation (TASK-1)") {
  SUBCASE("Creation, Suffix Preservation, and Automatic Cleanup") {
    std::string captured_path;
    {
      auto fixture = TestFixtures::create_ephemeral("minimal.dsk");
      captured_path = fixture.path();

      // Path must be non-empty, c_str() must match path()
      CHECK(!captured_path.empty());
      CHECK(std::string(fixture.c_str()) == captured_path);

      // Suffix must strictly terminate with .dsk (critical for format
      // autodetection)
      REQUIRE(captured_path.size() >= 4);
      CHECK(captured_path.compare(captured_path.size() - 4, 4, ".dsk") == 0);

      // File must exist on disk
      CHECK(access(captured_path.c_str(), F_OK) == 0);

      // Content length must match template
      std::ifstream f(captured_path, std::ios::binary | std::ios::ate);
      REQUIRE(f.is_open());
      CHECK(f.tellg() == 143360);  // Standard 140K DSK
    }
    // Scope exit: file must be unlinked
    CHECK(access(captured_path.c_str(), F_OK) != 0);
  }

  SUBCASE("Concurrent Ephemeral Clones (Multi-Drive Isolation)") {
    auto disk1 = TestFixtures::create_ephemeral("minimal.dsk");
    auto disk2 = TestFixtures::create_ephemeral("minimal.dsk");

    // Both must exist simultaneously with distinct paths
    CHECK(disk1.path() != disk2.path());
    CHECK(access(disk1.c_str(), F_OK) == 0);
    CHECK(access(disk2.c_str(), F_OK) == 0);
  }

  SUBCASE("Move Semantics, Single Ownership, and Self-Move") {
    std::string captured_path;
    {
      auto fixture1 = TestFixtures::create_ephemeral("minimal.woz");
      captured_path = fixture1.path();
      CHECK(!captured_path.empty());

      // Move construct
      EphemeralDiskFixture_t fixture2(std::move(fixture1));
      // NOLINTNEXTLINE(bugprone-use-after-move) - Explicitly validating post-move state
      CHECK(fixture1.path().empty());
      CHECK(fixture2.path() == captured_path);
      CHECK(access(captured_path.c_str(), F_OK) == 0);

      // Move assign (overwriting an existing fixture)
      EphemeralDiskFixture_t fixture3 =
          TestFixtures::create_ephemeral("minimal.dsk");
      std::string replaced_path = fixture3.path();
      CHECK(access(replaced_path.c_str(), F_OK) == 0);

      fixture3 = std::move(fixture2);
      // Replaced fixture temp file must have been unlinked
      CHECK(access(replaced_path.c_str(), F_OK) != 0);
      // fixture3 now owns captured_path
      CHECK(fixture3.path() == captured_path);
      // NOLINTNEXTLINE(bugprone-use-after-move) - Explicitly validating post-move state
      CHECK(fixture2.path().empty());
      CHECK(access(captured_path.c_str(), F_OK) == 0);

      // Self-move assignment safety check
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
      fixture3 = std::move(fixture3);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
      CHECK(fixture3.path() == captured_path);
      CHECK(access(captured_path.c_str(), F_OK) == 0);
    }
    CHECK(access(captured_path.c_str(), F_OK) != 0);
  }

  SUBCASE("Mutation Isolation and Template Invariance") {
    std::string template_path = TestFixtures::get_fixture_path("minimal.dsk");
    REQUIRE(access(template_path.c_str(), R_OK) == 0);

    // Read original template content bytes
    std::ifstream orig_file(template_path, std::ios::binary);
    REQUIRE(orig_file.is_open());
    std::string original_content((std::istreambuf_iterator<char>(orig_file)),
                                 std::istreambuf_iterator<char>());
    orig_file.close();

    {
      auto ephemeral = TestFixtures::create_ephemeral("minimal.dsk");
      // Corrupt / mutate the ephemeral copy
      std::ofstream mutate_stream(ephemeral.path(),
                                  std::ios::binary | std::ios::trunc);
      REQUIRE(mutate_stream.is_open());
      std::string junk_data(512, '\xAA');
      mutate_stream.write(junk_data.data(),
                          static_cast<std::streamsize>(junk_data.size()));
      mutate_stream.close();

      // Verify ephemeral copy has corrupted size
      std::ifstream mutated_file(ephemeral.path(),
                                 std::ios::binary | std::ios::ate);
      CHECK(mutated_file.tellg() == 512);
    }

    // Verify source template file remains bit-for-bit identical
    std::ifstream check_file(template_path, std::ios::binary);
    REQUIRE(check_file.is_open());
    std::string post_content((std::istreambuf_iterator<char>(check_file)),
                             std::istreambuf_iterator<char>());
    check_file.close();

    CHECK(original_content.size() == post_content.size());
    CHECK(original_content == post_content);
  }

  SUBCASE("Error Handling: Non-existent Template") {
    CHECK_THROWS_AS(
        TestFixtures::create_ephemeral("non_existent_fixture_12345.dsk"),
        std::runtime_error);
  }
}
