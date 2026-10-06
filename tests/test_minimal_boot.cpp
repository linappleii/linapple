// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>
#include <string>

#include "HeadlessHarness.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr uint32_t prompt_frame_cap = 300;
constexpr int text_rows = 24;

// The prompt alone on a row, with the cursor beside it: the II's Monitor
// draws the cursor as a flashing space, $60 (Apple II Reference Manual 1979
// p. 15, codes $40-$7F flash), while the //e's cursor byte is one the harness
// trims. The //e self-test's RAM patterns can put a "]" in column 0, so a
// prefix would not do.
auto is_prompt_row(const std::string& text) -> bool {
  constexpr char flashing_space = 0x60;
  return text == "]" || text == std::string("]") + flashing_space;
}

// Runs one frame at a time until a text row holds the prompt or the cap is
// spent.
auto prompt_row_within(HeadlessHarness_t& harness, uint32_t cap) -> bool {
  for (uint32_t frame = 0; frame < cap; ++frame) {
    harness.run_frames(1);
    for (int row = 0; row < text_rows; ++row) {
      if (is_prompt_row(harness.get_text_row(row))) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

TEST_CASE(
    "Minimal boot: DOS 3.3 on a II Plus with a Disk II and the speaker shows "
    "the prompt within 300 frames") {
  TestConfig_t::Description_t description = TestConfig_t::disk_ii_only();
  description.machine_type = TestConfig_t::machine_apple2_plus;
  TestConfig_t config(description);
  HeadlessHarness_t harness(config);
  auto disk = TestFixtures::create_ephemeral("Master.dsk");
  harness.mount_disk(6, 0, disk);
  harness.boot();
  CHECK(prompt_row_within(harness, prompt_frame_cap));
}
