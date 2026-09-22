// SPDX-License-Identifier: GPL-2.0-only
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "doctest.h"
#include "test_fixtures.h"

TEST_CASE("DiskNibble: a 6656-nibble image opens through the loader") {
  auto image = TestFixtures::create_ephemeral("minimal.nib");
  disk_loader_reset();

  const DiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(disk_loader_open(image.c_str(), &driver, &instance) == disk_err_none);
  REQUIRE(driver != nullptr);
  REQUIRE(instance != nullptr);
  CHECK(std::string(driver->name) == "NIB (6656-nibble)");
  driver->close(instance);
}
