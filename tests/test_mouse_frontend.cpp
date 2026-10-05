// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

constexpr int first_slot = 1;
constexpr int last_slot = 7;

}  // namespace

TEST_CASE(
    "Mouse frontend: a machine with no mouse card answers no slot's query") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  for (int slot = first_slot; slot <= last_slot; ++slot) {
    uint8_t active = 0;
    size_t size = sizeof(active);
    CHECK(peripheral_query_by_id(slot, "linapple.mouse", mouse_query_is_active,
                                 &active, &size) == peripheral_error);
  }
}
