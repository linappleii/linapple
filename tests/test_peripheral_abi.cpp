// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cstdint>
#include <string>

#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "doctest.h"

// Define the global peripheral provided by the C file
extern "C" Peripheral_t g_test_c_peripheral;

TEST_CASE("Peripheral ABI: Registration and I/O") {
  // We need to initialize memory system for io_map_dispatch to work
  // though for this specific test we might just want to test
  // peripheral_register and the proxies.

  peripheral_manager_init();

  // Register the C peripheral in Slot 1
  int result = peripheral_register(&g_test_c_peripheral, 1);
  CHECK(result == 0);

  // Verify I/O dispatch
  // In Slot 1, C090-C09F are the addresses
  // We wrote a simple handler that returns what was written.

  // Write 0x42 to $C090
  io_map_dispatch(0x0000, 0xC090, 1, 0x42, 0);

  // Read from $C090
  uint8_t val = io_map_dispatch(0x0000, 0xC090, 0, 0, 0);
  CHECK(val == 0x42);

  // Reset and check it cleared
  peripheral_manager_reset();
  val = io_map_dispatch(0x0000, 0xC090, 0, 0, 0);
  CHECK(val == 0);

  peripheral_manager_shutdown();
}

namespace {

HostInterface_t* g_captured_host = nullptr;

auto log_probe_init(int slot, HostInterface_t* host) -> void* {
  (void)slot;
  g_captured_host = host;
  return &g_captured_host;
}

Peripheral_t g_log_probe_peripheral = {LINAPPLE_ABI_VERSION,
                                       "test.log_probe",
                                       "LogProbe",
                                       "Captures the host interface",
                                       "LinApple Contributors",
                                       "1.0.0",
                                       PERIPHERAL_MASK_EXPANSION,
                                       -1,
                                       log_probe_init,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr};

std::string g_last_logged_message;

auto capture_log_message(LogLevel_t level, const char* message) -> void {
  (void)level;
  g_last_logged_message = (message != nullptr) ? message : "";
}

}  // namespace

TEST_CASE(
    "Peripheral ABI: A peripheral's log line reaches the sink formatted") {
  g_captured_host = nullptr;
  g_last_logged_message.clear();

  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_log_probe_peripheral, 2) == 0);
  REQUIRE(g_captured_host != nullptr);
  REQUIRE(g_captured_host->Log != nullptr);

  Logger::set_callback(capture_log_message);
  g_captured_host->Log(&g_captured_host, log_error, "%d %s", 42, "cells");
  Logger::set_callback(nullptr);

  CHECK(g_last_logged_message == "42 cells");

  peripheral_manager_shutdown();
}

#include <ctime>

#include "apple2/peripherals/Peripheral_Internal.h"
#include "test_fixtures_core.h"

extern "C" int test_c_peripheral_read_clock(HostInterface_t* host,
                                            int64_t* unix_seconds,
                                            uint8_t* weekday);

namespace {

static_assert(sizeof(HostLocalTime_t) == 24,
              "HostLocalTime_t is part of the plugin ABI");

// 2026-03-12 14:30:00 in Eastern Daylight Time (UTC-4), a Thursday:
//   date -u -d @1773340200                 -> 2026-03-12 18:30:00
//   TZ=America/New_York date -d @1773340200 -> 2026-03-12 14:30:00 -0400 Thu
// The UTC instant beside local fields is what tells a pass-through from a
// card that re-applied the zone.
constexpr HostLocalTime_t frozen_thursday = {1773340200, -14400, 2026, 3, 12,
                                             4,          14,     30,   0};

}  // namespace

TEST_CASE("Peripheral ABI: A frozen host clock reaches a card the core built") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  TestFixtures::ScopedLocalTimeProvider_t clock(frozen_thursday);

  g_captured_host = nullptr;
  REQUIRE(peripheral_register(&g_log_probe_peripheral, 2) == 0);
  REQUIRE(g_captured_host != nullptr);
  REQUIRE(g_captured_host->GetLocalTime != nullptr);

  HostLocalTime_t seen{};
  REQUIRE(g_captured_host->GetLocalTime(&seen));
  CHECK(seen.unix_seconds == 1773340200);
  CHECK(seen.utc_offset_seconds == -14400);
  CHECK(seen.year == 2026);
  CHECK(seen.month == 3);
  CHECK(seen.day_of_month == 12);
  CHECK(seen.weekday == 4);
  CHECK(seen.hour == 14);
  CHECK(seen.minute == 30);
  CHECK(seen.second == 0);
  CHECK(clock.calls() == 1);

  int64_t c_seconds = 0;
  uint8_t c_weekday = 0;
  CHECK(test_c_peripheral_read_clock(g_captured_host, &c_seconds, &c_weekday) ==
        1);
  CHECK(c_seconds == 1773340200);
  CHECK(c_weekday == 4);
  CHECK(clock.calls() == 2);

  peripheral_unregister(2);
}

TEST_CASE("Peripheral ABI: The frozen clock ends with its scope") {
  peripheral_manager_init();
  g_captured_host = nullptr;
  REQUIRE(peripheral_register(&g_log_probe_peripheral, 2) == 0);
  REQUIRE(g_captured_host != nullptr);

  {
    TestFixtures::ScopedLocalTimeProvider_t clock(frozen_thursday);
    HostLocalTime_t seen{};
    REQUIRE(g_captured_host->GetLocalTime(&seen));
    CHECK(seen.unix_seconds == 1773340200);
  }

  HostLocalTime_t after{};
  REQUIRE(g_captured_host->GetLocalTime(&after));
  CHECK(after.unix_seconds != 1773340200);
  CHECK(after.year >= 2026);

  peripheral_manager_shutdown();
}

TEST_CASE(
    "Peripheral ABI: Without a provider the host clock is the wall clock") {
  peripheral_manager_init();
  g_captured_host = nullptr;
  REQUIRE(peripheral_register(&g_log_probe_peripheral, 2) == 0);
  REQUIRE(g_captured_host != nullptr);

  const time_t before = time(nullptr);
  HostLocalTime_t seen{};
  REQUIRE(g_captured_host->GetLocalTime(&seen));
  const time_t after = time(nullptr);
  CHECK(seen.unix_seconds >= static_cast<int64_t>(before));
  CHECK(seen.unix_seconds <= static_cast<int64_t>(after));

  // Broken-down time fields must match the exact timestamp in the host's zone.
  time_t instant = static_cast<time_t>(seen.unix_seconds);
  struct tm local{};
  REQUIRE(localtime_r(&instant, &local) != nullptr);
  CHECK(seen.utc_offset_seconds == static_cast<int32_t>(local.tm_gmtoff));
  CHECK(seen.year == local.tm_year + 1900);
  CHECK(seen.month == local.tm_mon + 1);
  CHECK(seen.day_of_month == local.tm_mday);
  CHECK(seen.weekday == local.tm_wday);
  CHECK(seen.hour == local.tm_hour);
  CHECK(seen.minute == local.tm_min);
  CHECK(seen.second == std::min(local.tm_sec, 59));

  CHECK(g_captured_host->GetLocalTime(nullptr) == false);

  peripheral_manager_shutdown();
}

TEST_CASE("Peripheral ABI: A host without a clock says so and writes nothing") {
  peripheral_manager_init();
  g_captured_host = nullptr;
  REQUIRE(peripheral_register(&g_log_probe_peripheral, 2) == 0);
  REQUIRE(g_captured_host != nullptr);

  linapple_set_local_time_provider(
      [](void*, HostLocalTime_t*) -> bool { return false; }, nullptr);
  HostLocalTime_t untouched{};
  untouched.year = 1234;
  CHECK(g_captured_host->GetLocalTime(&untouched) == false);
  CHECK(untouched.year == 1234);
  linapple_set_local_time_provider(nullptr, nullptr);

  peripheral_manager_shutdown();
}
