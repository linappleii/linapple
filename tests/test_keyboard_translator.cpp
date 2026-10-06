// SPDX-License-Identifier: GPL-2.0-only

#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/KeyboardTranslator.h"

TEST_CASE(
    "Keyboard translator: HID scancode 4 is the positional key $504 and "
    "scancode 3 has no key") {
  CHECK(keyboard_scancode_to_positional(4) == 0x504);
  CHECK(keyboard_scancode_to_positional(3) == linapple_key_unknown);
}
