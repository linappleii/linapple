// SPDX-License-Identifier: GPL-2.0-only
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include "HeadlessHarness.h"
#include "Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "doctest.h"
#include "test_fixtures.h"

TEST_CASE("Headless: [HL-01] Boot from --d1") {
  HeadlessHarness_t harness;

  auto disk1 = TestFixtures::create_ephemeral("minimal.woz");
  harness.mount_disk(6, 0, disk1);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);

  // E2E boot sequence: reset CPU/system and advance virtual frames
  harness.boot();
  harness.run_frames(20);
  CHECK(harness.get_text_row(0).find("Apple //e") != std::string::npos);
}

TEST_CASE("Headless: [HL-02] Both drives loaded") {
  HeadlessHarness_t harness;

  auto disk1 = TestFixtures::create_ephemeral("minimal.woz");
  auto disk2 = TestFixtures::create_ephemeral("minimal.dsk");
  harness.mount_disk(6, 0, disk1);
  harness.mount_disk(6, 1, disk2);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive1_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);
  CHECK(status.drive1_last_error == disk_err_none);
}

TEST_CASE("Headless: [HL-03] Unsupported file") {
  HeadlessHarness_t harness;

  // .txt is unsupported by disk drivers
  auto disk = TestFixtures::create_ephemeral("minimal.txt");
  harness.mount_disk(6, 0, disk);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_unsupported_format);
}

TEST_CASE("Headless: [HL-04] Program loading") {
  HeadlessHarness_t harness;

  auto prog = TestFixtures::create_ephemeral("minimal.woz");
  int err = linapple_load_program(prog.c_str());
  CHECK(err != 0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);
  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == false);
}

TEST_CASE("Headless: [HL-05] Video worker thread wakeup and frame readiness") {
  HeadlessHarness_t harness;

  REQUIRE(video_init_worker() == true);

  g_frame_ready = false;
  video_refresh_screen(0, true);

  // Give worker thread a moment to wake up and process the refresh
  for (int i = 0; i < 50 && !g_frame_ready; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  CHECK(g_frame_ready == true);
}

TEST_CASE(
    "Headless: [HL-06] Text screen rendering produces golden visual output") {
  HeadlessHarness_t harness;

  uint8_t* text_page = get_mem_ptr(0x0400);
  REQUIRE(text_page != nullptr);

  // Clear text page 1 (0x0400 - 0x07FF) to Apple II normal space (0xA0)
  std::memset(text_page, 0xA0, 0x0400);

  // 1. Write "APPLE" in text screen memory (Row 0: 0x0400)
  text_page[0] = 0xC1;  // 'A' | 0x80
  text_page[1] = 0xD0;  // 'P' | 0x80
  text_page[2] = 0xD0;  // 'P' | 0x80
  text_page[3] = 0xCC;  // 'L' | 0x80
  text_page[4] = 0xC5;  // 'E' | 0x80

  // 2. Inspect decoded text screen row 0
  CHECK(harness.get_text_row(0) == "APPLE");

  // 3. Inspect rasterized output buffer against exact golden frame CRC32
  harness.assert_screen_matches(0x6467AC7E);
}
