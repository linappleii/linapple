// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "doctest.h"

TEST_CASE(
    "Printer Frontend: the card the frontend prints through is built in") {
  const Peripheral_t* descriptor = peripheral_find_internal("linapple.printer");
  REQUIRE(descriptor != nullptr);
  CHECK(std::string(descriptor->name) == "Parallel Printer");
}
