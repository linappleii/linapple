// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "core/LinAppleCore.h"
#include "doctest.h"

TEST_CASE("Core: Emulation Speed Controls and Multipliers") {
  linapple_set_speed(SPEED_NORMAL);
  CHECK(linapple_get_speed() == SPEED_NORMAL);

  g_state.clks_per_frame = 17030;
  CHECK(linapple_get_frame_cycles() == 17030);

  // Increase speed (+2 per step)
  CHECK(linapple_speed_increase() == 12);
  CHECK(linapple_get_speed() == 12);
  CHECK(linapple_get_frame_cycles() == 20436);

  // Set speed to 2x (20)
  linapple_set_speed(20);
  CHECK(linapple_get_speed() == 20);
  CHECK(linapple_get_frame_cycles() == 34060);

  // Decrease speed (-1 per step)
  CHECK(linapple_speed_decrease() == 19);
  CHECK(linapple_get_speed() == 19);

  // Reset speed to normal (10)
  CHECK(linapple_speed_reset() == SPEED_NORMAL);
  CHECK(linapple_get_speed() == SPEED_NORMAL);
  CHECK(linapple_get_frame_cycles() == 17030);

  // Slow motion: speed 0 (0.5x)
  linapple_set_speed(0);
  CHECK(linapple_get_speed() == 0);
  CHECK(linapple_get_frame_cycles() == 8515);

  // Clamp at max
  linapple_set_speed(100);
  CHECK(linapple_get_speed() == emulation_speed_max);

  // Restore normal speed
  linapple_speed_reset();
}

TEST_CASE("Core: Turbo Mode Toggle") {
  linapple_set_turbo(false);
  CHECK(linapple_get_turbo() == false);

  CHECK(linapple_toggle_turbo() == true);
  CHECK(linapple_get_turbo() == true);

  CHECK(linapple_toggle_turbo() == false);
  CHECK(linapple_get_turbo() == false);

  linapple_set_turbo(true);
  CHECK(linapple_get_turbo() == true);
  linapple_set_turbo(false);
}
