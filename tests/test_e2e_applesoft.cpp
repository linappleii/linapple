// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "HeadlessHarness.h"
#include "doctest.h"
#include "test_fixtures.h"

TEST_CASE("Headless E2E: Boot and Applesoft Expression Evaluation (TASK-2)") {
  HeadlessHarness_t harness;

  SUBCASE("Cold boot, monitor prompt, and Applesoft entry") {
    auto disk = TestFixtures::create_ephemeral("minimal.dsk");
    harness.mount_disk(6, 0, disk);
    harness.boot();
    harness.run_frames(20);

    // Initial Apple //e power-on header
    CHECK(harness.get_text_row(0).find("Apple //e") != std::string::npos);

    // Enter Applesoft BASIC from monitor
    harness.type_string("E000G\r", 4);
    harness.run_frames(20);

    // Prompt row should contain Applesoft ']' prompt
    CHECK(harness.get_text_row(5) == "]");
  }

  SUBCASE("Evaluation of arithmetic expression (PRINT 2+2)") {
    auto disk = TestFixtures::create_ephemeral("minimal.dsk");
    harness.mount_disk(6, 0, disk);
    harness.boot();
    harness.run_frames(20);

    // Enter Applesoft BASIC and clear screen
    harness.type_string("E000G\r", 4);
    harness.run_frames(20);
    harness.type_string("HOME\r", 4);
    harness.run_frames(20);

    CHECK(harness.get_text_row(1) == "]");

    // Execute PRINT 2+2
    harness.type_string("PRINT 2+2\r", 4);
    harness.run_frames(20);

    // Row 1 displays entered command, Row 2 displays evaluated answer
    CHECK(harness.get_text_row(1) == "]PRINT 2+2");
    CHECK(harness.get_text_row(2) == "4");

    // Verify audio samples were emitted and screen matches golden CRC
    CHECK(harness.get_audio_sample_count() > 0);
    harness.assert_screen_matches(0xEAA5455E);
  }

  SUBCASE("Compound statement on cleared screen (HOME:PRINT 2+2)") {
    auto disk = TestFixtures::create_ephemeral("minimal.dsk");
    harness.mount_disk(6, 0, disk);
    harness.boot();
    harness.run_frames(20);

    // Jump to Applesoft and execute compound statement
    harness.type_string("E000G\r", 4);
    harness.run_frames(20);
    harness.type_string("HOME:PRINT 2+2\r", 4);
    harness.run_frames(20);

    // Output is displayed directly at row 0
    CHECK(harness.get_text_row(0) == "4");
    harness.assert_screen_matches(0x11b89b6f);
  }
}
