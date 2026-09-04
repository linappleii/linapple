// SPDX-License-Identifier: GPL-2.0-only
#include <atomic>
#include <cstdint>

#include "Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "doctest.h"
#include "test_fixtures.h"

// Since Main.cpp is already linked into 'linapple' (headless target),
// we can't easily link it here. We'll implement a test that replicates
// the logic of Main.cpp but with assertions.

TEST_CASE("Headless: [HL-01] Boot from --d1") {
  linapple_init();

  Configuration_t::instance().set_string(
      "Slots", REGVALUE_DISK_IMAGE1,
      TestFixtures::get_fixture_path("minimal.woz"));

  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive0_last_error == disk_err_none);

  linapple_shutdown();
}

TEST_CASE("Headless: [HL-02] Both drives loaded") {
  linapple_init();

  Configuration_t::instance().set_string(
      "Slots", REGVALUE_DISK_IMAGE1,
      TestFixtures::get_fixture_path("minimal.woz"));
  Configuration_t::instance().set_string(
      "Slots", REGVALUE_DISK_IMAGE2,
      TestFixtures::get_fixture_path("minimal.dsk"));

  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  CHECK(status.drive0_loaded == true);
  CHECK(status.drive1_loaded == true);

  linapple_shutdown();
}

TEST_CASE("Headless: [HL-03] Unsupported file") {
  linapple_init();

  // .txt is unsupported by disk drivers
  Configuration_t::instance().set_string(
      "Slots", REGVALUE_DISK_IMAGE1,
      TestFixtures::get_fixture_path("minimal.txt"));

  peripheral_manager_init();
  linapple_register_peripherals();

  DiskStatus_t status{};
  size_t size = sizeof(status);
  PeripheralStatus_t ps =
      peripheral_query(6, disk_cmd_get_status, &status, &size);

  REQUIRE(ps == peripheral_ok);
  // It shouldn't be loaded, and error should be unsupported format (or file not
  // found if it doesn't exist) Actually our minimal.txt doesn't exist in
  // fixtures yet? I'll check. Assuming it's unsupported if it exists but isn't
  // a disk.
  CHECK(status.drive0_loaded == false);
  CHECK(status.drive0_last_error == disk_err_unsupported_format);

  linapple_shutdown();
}

TEST_CASE("Headless: [HL-04] Program loading") {
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  int err = linapple_load_program(
      TestFixtures::get_fixture_path("minimal.woz").c_str());
  CHECK(err != 0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  peripheral_query(6, disk_cmd_get_status, &status, &size);
  CHECK(status.drive0_loaded == false);

  linapple_shutdown();
}

TEST_CASE("Headless: [HL-05] Video worker thread wakeup and frame readiness") {
  extern auto video_init_worker() -> bool;
  extern auto video_refresh_screen(uint32_t mode = 0, bool redraw_whole = false)
      -> void;
  extern std::atomic<bool> g_frame_ready;

  linapple_init();
  video_init_worker();

  g_frame_ready = false;
  video_refresh_screen(0, true);

  // Give worker thread a moment to wake up and process the refresh
  for (int i = 0; i < 50 && !g_frame_ready; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  CHECK(g_frame_ready == true);

  linapple_shutdown();
}

TEST_CASE(
    "Headless: [HL-06] Text screen rendering produces non-black character "
    "pixels") {
  extern auto video_redraw_screen() -> void;
  extern auto video_get_output_buffer() -> uint32_t*;
  extern uint8_t* mem;

  linapple_init();
  g_state.mode = MODE_RUNNING;

  // 1. Write "APPLE" in text screen memory (Row 0: 0x0400) with flashing/normal
  // characters
  mem[0x0400] = 0xC1;  // 'A' | 0x80
  mem[0x0401] = 0xD0;  // 'P' | 0x80
  mem[0x0402] = 0xD0;  // 'P' | 0x80
  mem[0x0403] = 0xCC;  // 'L' | 0x80
  mem[0x0404] = 0xC5;  // 'E' | 0x80

  // 2. Trigger full video redraw
  video_redraw_screen();

  // 3. Inspect output buffer
  uint32_t* output = video_get_output_buffer();
  REQUIRE(output != nullptr);

  size_t non_black_pixels = 0;
  for (size_t i = 0; i < 560 * 384; ++i) {
    if ((output[i] & 0x00FFFFFF) != 0) {
      non_black_pixels++;
    }
  }

  // Without charset40 loaded, DrawTextSource early-returns and non_black_pixels
  // is exactly 0. With font glyphs properly loaded, the letters "APPLE" render
  // non-zero pixels.
  CHECK(non_black_pixels > 0);

  linapple_shutdown();
}
