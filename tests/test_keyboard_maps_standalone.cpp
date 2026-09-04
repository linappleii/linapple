// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>

#include "apple2/peripherals/keyboard/Keyboard_Maps.h"
#include "doctest.h"

TEST_CASE("Keyboard Maps: Map_US sanity check") {
  CHECK(strcmp(map_us.name, "US") == 0);

  // Physical 'A' key location should produce ASCII 'a'
  CHECK(map_us.map[keyb_idx_a] == 'a');
  CHECK(map_us.map[keyb_idx_1] == '1');
  CHECK(map_us.map[keyb_idx_space] == 0x20);
  CHECK(map_us.map[keyb_idx_return] == 0x0D);
}

TEST_CASE("Keyboard Maps: Map_FR sanity check") {
  CHECK(strcmp(map_fr.name, "French") == 0);

  // Physical 'Q' key on US keyboard (Row 2, Col 1) is 'A' on French AZERTY
  CHECK(map_fr.map[keyb_idx_q] == 'a');
  // Physical 'A' key on US keyboard (Row 3, Col 1) is 'Q' on French AZERTY
  CHECK(map_fr.map[keyb_idx_a] == 'q');
}
