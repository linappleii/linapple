// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/formats/DiskContainer.h"
#include "doctest.h"
#include "test_fixtures.h"

TEST_CASE("DiskContainer: a zip archive names the image it carries") {
  const std::string archive = TestFixtures::get_fixture_path("minimal.dsk.zip");
  std::array<char, 256> name{};
  REQUIRE(
      disk_container_payload_name(archive.c_str(), name.data(), name.size()));
  CHECK(std::string(name.data()) == "minimal.dsk");
}
