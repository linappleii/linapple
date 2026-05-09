#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "apple2/peripherals/Keyboard_Maps.h"
#include <cstring>

TEST_CASE("Keyboard Maps: Map_US sanity check") {
  CHECK(strcmp(Map_US.name, "US") == 0);

  // Physical 'A' key location should produce ASCII 'a'
  CHECK(Map_US.map[KEYB_IDX_A] == 'a');
  CHECK(Map_US.map[KEYB_IDX_1] == '1');
  CHECK(Map_US.map[KEYB_IDX_SPACE] == 0x20);
  CHECK(Map_US.map[KEYB_IDX_RETURN] == 0x0D);
}

TEST_CASE("Keyboard Maps: Map_FR sanity check") {
  CHECK(strcmp(Map_FR.name, "French") == 0);

  // Physical 'Q' key on US keyboard (Row 2, Col 1) is 'A' on French AZERTY
  CHECK(Map_FR.map[KEYB_IDX_Q] == 'a');
  // Physical 'A' key on US keyboard (Row 3, Col 1) is 'Q' on French AZERTY
  CHECK(Map_FR.map[KEYB_IDX_A] == 'q');
}
