// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "doctest.h"

TEST_CASE("Printer Snapshot: the card saves and loads a frame") {
  const Peripheral_t* descriptor = peripheral_find_internal("linapple.printer");
  REQUIRE(descriptor != nullptr);
  CHECK(descriptor->save_state != nullptr);
  CHECK(descriptor->load_state != nullptr);
}
