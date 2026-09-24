// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
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

#include <cstddef>

extern "C" int test_c_peripheral_sink_write(HostInterface_t* host, int slot,
                                            uint8_t byte);

namespace {

// Every host member is a function pointer, so the offsets are pinned as
// member counts: the same header must lay out the same on a 32-bit host.
constexpr size_t host_member_size = sizeof(void (*)(void));

static_assert(offsetof(HostInterface_t, PrinterPutChar) ==
                  16 * host_member_size,
              "PrinterPutChar moved");
static_assert(offsetof(HostInterface_t, PrinterGetStatus) ==
                  17 * host_member_size,
              "PrinterGetStatus moved");
static_assert(offsetof(HostInterface_t, GetLocalTime) == 21 * host_member_size,
              "GetLocalTime moved");
static_assert(offsetof(HostInterface_t, SinkOpen) == 22 * host_member_size,
              "SinkOpen is not the first member after GetLocalTime");
static_assert(offsetof(HostInterface_t, SinkWrite) == 23 * host_member_size,
              "SinkWrite moved");
static_assert(offsetof(HostInterface_t, SinkReady) == 24 * host_member_size,
              "SinkReady moved");
static_assert(offsetof(HostInterface_t, SinkClose) == 25 * host_member_size,
              "SinkClose moved");
static_assert(sizeof(HostInterface_t) == 26 * host_member_size,
              "HostInterface_t grew past the sink members");
static_assert(peripheral_sink_printer == 1 && peripheral_sink_serial == 2,
              "PeripheralSinkKind_t values are part of the plugin ABI");

// A card that opens its sink at init and, when asked, records what the
// manager had done for the sink by the time its own hooks ran.
struct SinkProbe_t {
  HostInterface_t* host = nullptr;
  void* token = nullptr;
  int slot = 0;
  unsigned ticks_seen_at_think = 0;
  unsigned ticks_seen_at_command = 0;
  bool command_seen = false;
};

SinkProbe_t g_sink_probe;
const TestFixtures::ScopedByteSink_t* g_sink_watched_by_probe = nullptr;

auto sink_probe_init(int slot, HostInterface_t* host) -> void* {
  g_sink_probe = SinkProbe_t();
  g_sink_probe.host = host;
  g_sink_probe.slot = slot;
  g_sink_probe.token =
      host->SinkOpen(&g_sink_probe, slot, peripheral_sink_printer);
  return &g_sink_probe;
}

auto sink_probe_shutdown(void* instance) -> void {
  auto* probe = static_cast<SinkProbe_t*>(instance);
  probe->host->SinkClose(probe->token);
}

auto sink_probe_think(void* instance, uint32_t cycles) -> void {
  (void)cycles;
  auto* probe = static_cast<SinkProbe_t*>(instance);
  if (g_sink_watched_by_probe != nullptr) {
    probe->ticks_seen_at_think = g_sink_watched_by_probe->ticks();
  }
}

auto sink_probe_command(void* instance, uint32_t cmd_id, const void* data,
                        size_t size) -> PeripheralStatus_t {
  (void)cmd_id;
  (void)data;
  (void)size;
  auto* probe = static_cast<SinkProbe_t*>(instance);
  probe->command_seen = true;
  if (g_sink_watched_by_probe != nullptr) {
    probe->ticks_seen_at_command = g_sink_watched_by_probe->ticks();
  }
  return peripheral_ok;
}

Peripheral_t g_sink_probe_peripheral = {LINAPPLE_ABI_VERSION,
                                        "test.sink_probe",
                                        "SinkProbe",
                                        "Opens a byte sink at init",
                                        "LinApple Contributors",
                                        "1.0.0",
                                        PERIPHERAL_MASK_EXPANSION,
                                        -1,
                                        sink_probe_init,
                                        nullptr,
                                        sink_probe_shutdown,
                                        sink_probe_think,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        sink_probe_command,
                                        nullptr};

constexpr int probe_slot = 2;

// Puts the bridge in the state a fresh process has: nothing installed. The
// previous binding is restored so that a session-wide sink, if one is ever
// installed by the harness, survives the case.
class ScopedNoByteSink_t {
 public:
  ScopedNoByteSink_t() : previous_(linapple_set_byte_sink(nullptr, nullptr)) {}
  ~ScopedNoByteSink_t() {
    linapple_set_byte_sink(previous_.vtable, previous_.ctx);
  }
  ScopedNoByteSink_t(const ScopedNoByteSink_t&) = delete;
  auto operator=(const ScopedNoByteSink_t&) -> ScopedNoByteSink_t& = delete;
  ScopedNoByteSink_t(ScopedNoByteSink_t&&) = delete;
  auto operator=(ScopedNoByteSink_t&&) -> ScopedNoByteSink_t& = delete;

 private:
  ByteSinkBinding_t previous_;
};

}  // namespace

TEST_CASE(
    "Peripheral ABI: The sink members follow GetLocalTime at pinned offsets") {
  CHECK(offsetof(HostInterface_t, SinkOpen) ==
        offsetof(HostInterface_t, GetLocalTime) + host_member_size);
  CHECK(offsetof(HostInterface_t, SinkClose) + host_member_size ==
        sizeof(HostInterface_t));

  peripheral_manager_init();
  g_captured_host = nullptr;
  REQUIRE(peripheral_register(&g_log_probe_peripheral, probe_slot) == 0);
  REQUIRE(g_captured_host != nullptr);
  CHECK(g_captured_host->SinkOpen != nullptr);
  CHECK(g_captured_host->SinkWrite != nullptr);
  CHECK(g_captured_host->SinkReady != nullptr);
  CHECK(g_captured_host->SinkClose != nullptr);
  // The retired pair still forwards until the card stops using it.
  CHECK(g_captured_host->PrinterPutChar != nullptr);
  CHECK(g_captured_host->PrinterGetStatus != nullptr);
  peripheral_manager_shutdown();
}

TEST_CASE(
    "Peripheral ABI: Without a host sink the token exists, is not ready and "
    "swallows bytes") {
  ScopedNoByteSink_t nothing_installed;
  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_sink_probe_peripheral, probe_slot) == 0);
  REQUIRE(g_sink_probe.token != nullptr);

  HostInterface_t* host = g_sink_probe.host;
  CHECK(host->SinkReady(g_sink_probe.token) == false);
  host->SinkWrite(g_sink_probe.token, 0xC8);
  CHECK(host->SinkReady(g_sink_probe.token) == false);

  // A NULL token and a token the bridge never minted are refused the same
  // way, never dereferenced.
  int not_a_token = 0;
  CHECK(host->SinkReady(nullptr) == false);
  CHECK(host->SinkReady(&not_a_token) == false);
  host->SinkWrite(nullptr, 0xC8);
  host->SinkWrite(&not_a_token, 0xC8);
  host->SinkClose(nullptr);
  host->SinkClose(&not_a_token);

  peripheral_manager_think(0);
  peripheral_manager_shutdown();
}

TEST_CASE(
    "Peripheral ABI: A card's bytes reach the installed sink with its slot") {
  TestFixtures::ScopedByteSink_t sink;
  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_sink_probe_peripheral, probe_slot) == 0);
  REQUIRE(g_sink_probe.token != nullptr);
  CHECK(sink.opens() == 0);

  HostInterface_t* host = g_sink_probe.host;
  CHECK(host->SinkReady(g_sink_probe.token));
  CHECK(sink.opens() == 1);
  CHECK(sink.last_open_kind() == peripheral_sink_printer);
  CHECK(sink.ready_polls() == 1);

  host->SinkWrite(g_sink_probe.token, 0xC8);
  host->SinkWrite(g_sink_probe.token, 0xC5);
  CHECK(test_c_peripheral_sink_write(host, probe_slot, 0x8D) == 1);
  CHECK(sink.opens() == 1);

  REQUIRE(sink.bytes().size() == 3);
  CHECK(sink.bytes()[0].slot == probe_slot);
  CHECK(sink.bytes()[0].byte == 0xC8);
  CHECK(sink.bytes()[1].slot == probe_slot);
  CHECK(sink.bytes()[1].byte == 0xC5);
  CHECK(sink.bytes()[2].slot == probe_slot);
  CHECK(sink.bytes()[2].byte == 0x8D);
  CHECK(sink.dropped() == 0);

  peripheral_manager_shutdown();
  CHECK(sink.closes() == 1);
}

TEST_CASE(
    "Peripheral ABI: A sink installed after the card opened its token "
    "receives the bytes") {
  ScopedNoByteSink_t nothing_installed;
  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_sink_probe_peripheral, probe_slot) == 0);
  HostInterface_t* host = g_sink_probe.host;
  host->SinkWrite(g_sink_probe.token, 0x01);

  {
    TestFixtures::ScopedByteSink_t late;
    CHECK(late.opens() == 0);
    host->SinkWrite(g_sink_probe.token, 0xC8);
    CHECK(late.opens() == 1);
    CHECK(late.last_open_kind() == peripheral_sink_printer);
    REQUIRE(late.bytes().size() == 1);
    CHECK(late.bytes()[0].slot == probe_slot);
    CHECK(late.bytes()[0].byte == 0xC8);
    CHECK(late.closes() == 0);
  }
  // The guard's departure closed what it had opened; with nothing behind the
  // host again the token is still valid and merely not ready.
  CHECK(host->SinkReady(g_sink_probe.token) == false);
  host->SinkWrite(g_sink_probe.token, 0xC5);

  peripheral_manager_shutdown();
}

TEST_CASE("Peripheral ABI: Nested sinks take over and restore in order") {
  ScopedNoByteSink_t nothing_installed;
  TestFixtures::ScopedByteSink_t outer;
  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_sink_probe_peripheral, probe_slot) == 0);
  HostInterface_t* host = g_sink_probe.host;

  host->SinkWrite(g_sink_probe.token, 0x01);
  CHECK(outer.opens() == 1);
  REQUIRE(outer.bytes().size() == 1);

  {
    TestFixtures::ScopedByteSink_t inner;
    // Installing the inner sink closed the slot under the outer one.
    CHECK(outer.closes() == 1);
    host->SinkWrite(g_sink_probe.token, 0x02);
    CHECK(inner.opens() == 1);
    REQUIRE(inner.bytes().size() == 1);
    CHECK(inner.bytes()[0].byte == 0x02);
    CHECK(outer.bytes().size() == 1);
    {
      TestFixtures::ScopedByteSink_t innermost;
      CHECK(inner.closes() == 1);
      host->SinkWrite(g_sink_probe.token, 0x03);
      REQUIRE(innermost.bytes().size() == 1);
      CHECK(innermost.bytes()[0].byte == 0x03);
      CHECK(inner.bytes().size() == 1);
    }
    host->SinkWrite(g_sink_probe.token, 0x04);
    CHECK(inner.opens() == 2);
    REQUIRE(inner.bytes().size() == 2);
    CHECK(inner.bytes()[1].byte == 0x04);
    CHECK(outer.bytes().size() == 1);
  }

  host->SinkWrite(g_sink_probe.token, 0x05);
  CHECK(outer.opens() == 2);
  REQUIRE(outer.bytes().size() == 2);
  CHECK(outer.bytes()[1].slot == probe_slot);
  CHECK(outer.bytes()[1].byte == 0x05);

  peripheral_manager_shutdown();
  CHECK(outer.closes() == 2);
}

TEST_CASE(
    "Peripheral ABI: SinkOpen refuses a slot outside 1..7, an unknown kind and "
    "a second kind on an open slot") {
  TestFixtures::ScopedByteSink_t sink;
  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_sink_probe_peripheral, probe_slot) == 0);
  HostInterface_t* host = g_sink_probe.host;
  void* instance = &g_sink_probe;

  CHECK(host->SinkOpen(instance, 0, peripheral_sink_printer) == nullptr);
  CHECK(host->SinkOpen(instance, 8, peripheral_sink_printer) == nullptr);
  CHECK(host->SinkOpen(instance, -1, peripheral_sink_printer) == nullptr);
  CHECK(host->SinkOpen(instance, 1, static_cast<PeripheralSinkKind_t>(0)) ==
        nullptr);
  CHECK(host->SinkOpen(instance, 1, static_cast<PeripheralSinkKind_t>(3)) ==
        nullptr);
  CHECK(host->SinkOpen(instance, 1, peripheral_sink_printer) != nullptr);
  CHECK(host->SinkOpen(instance, 7, peripheral_sink_serial) != nullptr);

  // Until the first use nothing is open, so the slot may still change kind.
  CHECK(host->SinkOpen(instance, probe_slot, peripheral_sink_serial) ==
        g_sink_probe.token);
  CHECK(host->SinkOpen(instance, probe_slot, peripheral_sink_printer) ==
        g_sink_probe.token);

  host->SinkWrite(g_sink_probe.token, 0xC8);
  CHECK(sink.opens() == 1);
  CHECK(host->SinkOpen(instance, probe_slot, peripheral_sink_serial) ==
        nullptr);
  CHECK(host->SinkOpen(instance, probe_slot, peripheral_sink_printer) ==
        g_sink_probe.token);
  CHECK(sink.opens() == 1);

  host->SinkClose(g_sink_probe.token);
  CHECK(sink.closes() == 1);
  void* serial = host->SinkOpen(instance, probe_slot, peripheral_sink_serial);
  REQUIRE(serial != nullptr);
  host->SinkWrite(serial, 0x41);
  CHECK(sink.opens() == 2);
  CHECK(sink.last_open_kind() == peripheral_sink_serial);
  REQUIRE(sink.bytes().size() == 2);
  CHECK(sink.bytes()[1].byte == 0x41);

  peripheral_manager_shutdown();
}

TEST_CASE(
    "Peripheral ABI: A sink that is not ready drops the byte and says so") {
  TestFixtures::ScopedByteSink_t sink;
  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_sink_probe_peripheral, probe_slot) == 0);
  HostInterface_t* host = g_sink_probe.host;

  sink.set_ready(false);
  CHECK(host->SinkReady(g_sink_probe.token) == false);
  host->SinkWrite(g_sink_probe.token, 0xC8);
  CHECK(sink.dropped() == 1);
  CHECK(sink.bytes().empty());
  CHECK(test_c_peripheral_sink_write(host, probe_slot, 0xC5) == 0);
  CHECK(sink.dropped() == 2);

  sink.set_ready(true);
  CHECK(host->SinkReady(g_sink_probe.token));
  host->SinkWrite(g_sink_probe.token, 0xCC);
  REQUIRE(sink.bytes().size() == 1);
  CHECK(sink.bytes()[0].byte == 0xCC);
  CHECK(sink.dropped() == 2);
  // The C helper polls readiness before it writes, so three polls in all.
  CHECK(sink.ready_polls() == 3);

  peripheral_manager_shutdown();
}

TEST_CASE(
    "Peripheral ABI: SinkClose closes an open slot once and the next use "
    "reopens it") {
  TestFixtures::ScopedByteSink_t sink;
  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_sink_probe_peripheral, probe_slot) == 0);
  HostInterface_t* host = g_sink_probe.host;

  // Closing what was never opened opens nothing and closes nothing.
  host->SinkClose(g_sink_probe.token);
  CHECK(sink.opens() == 0);
  CHECK(sink.closes() == 0);

  host->SinkWrite(g_sink_probe.token, 0xC8);
  CHECK(sink.opens() == 1);
  host->SinkClose(g_sink_probe.token);
  CHECK(sink.closes() == 1);
  host->SinkClose(g_sink_probe.token);
  CHECK(sink.closes() == 1);

  // A re-init under an unchanged sink mints the same token and reopens on
  // first use.
  void* again =
      host->SinkOpen(&g_sink_probe, probe_slot, peripheral_sink_printer);
  CHECK(again == g_sink_probe.token);
  CHECK(host->SinkReady(again));
  CHECK(sink.opens() == 2);

  peripheral_manager_shutdown();
  CHECK(sink.closes() == 2);
}

TEST_CASE(
    "Peripheral ABI: The manager ticks the sink after the command drain and "
    "before any card thinks") {
  TestFixtures::ScopedByteSink_t sink;
  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_sink_probe_peripheral, probe_slot) == 0);
  g_sink_watched_by_probe = &sink;

  REQUIRE(peripheral_command(probe_slot, 0x00010001, nullptr, 0) ==
          peripheral_ok);
  peripheral_manager_think(0);
  CHECK(sink.ticks() == 1);
  CHECK(g_sink_probe.command_seen);
  CHECK(g_sink_probe.ticks_seen_at_command == 0);
  CHECK(g_sink_probe.ticks_seen_at_think == 1);

  peripheral_manager_think(1000);
  CHECK(sink.ticks() == 2);
  CHECK(g_sink_probe.ticks_seen_at_think == 2);

  g_sink_watched_by_probe = nullptr;
  peripheral_manager_shutdown();
}

TEST_CASE("Peripheral ABI: A sink without a tick is left alone") {
  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_sink_probe_peripheral, probe_slot) == 0);

  static unsigned writes_seen = 0;
  writes_seen = 0;
  static const ByteSink_t tickless = {
      nullptr, [](void*, int, uint8_t) -> void { ++writes_seen; }, nullptr,
      nullptr, nullptr};
  const ByteSinkBinding_t previous = linapple_set_byte_sink(&tickless, nullptr);

  peripheral_manager_think(0);
  HostInterface_t* host = g_sink_probe.host;
  host->SinkWrite(g_sink_probe.token, 0xC8);
  CHECK(writes_seen == 1);
  // No ready member means the sink has nothing to say, which reads as not
  // ready; no open or close member is skipped, not dereferenced.
  CHECK(host->SinkReady(g_sink_probe.token) == false);
  host->SinkClose(g_sink_probe.token);

  linapple_set_byte_sink(previous.vtable, previous.ctx);
  peripheral_manager_shutdown();
}

namespace {

// Two images a card might present from its PROM. Both open with SEC and a
// BCS, so two single steps from the top of the page land where the
// displacement says, and the displacement is an operand byte: the 6502 reads
// it from the live page, never through the I/O dispatch. The last byte tells
// a data read which image is in view. Everything else is RTS.
constexpr uint8_t opcode_sec = 0x38;
constexpr uint8_t opcode_bcs = 0xB0;
constexpr uint8_t opcode_rts = 0x60;
constexpr uint8_t opcode_lda_abs = 0xAD;
constexpr uint8_t image_a_displacement = 0x10;
constexpr uint8_t image_b_displacement = 0x20;
constexpr uint8_t image_a_tail = 0xAA;
constexpr uint8_t image_b_tail = 0x55;

auto cx_image(uint8_t displacement, uint8_t tail) -> std::array<uint8_t, 256> {
  std::array<uint8_t, 256> image{};
  image.fill(opcode_rts);
  image[0x00] = opcode_sec;
  image[0x01] = opcode_bcs;
  image[0x02] = displacement;
  image[0xFF] = tail;
  return image;
}

const std::array<uint8_t, 256> g_cx_image_a =
    cx_image(image_a_displacement, image_a_tail);
const std::array<uint8_t, 256> g_cx_image_b =
    cx_image(image_b_displacement, image_b_tail);

struct CxProbe_t {
  HostInterface_t* host = nullptr;
  int slot = 0;
};

CxProbe_t g_cx_probe;

auto cx_probe_init(int slot, HostInterface_t* host) -> void* {
  g_cx_probe = CxProbe_t();
  g_cx_probe.host = host;
  g_cx_probe.slot = slot;
  // Registering I/O with no handlers of its own puts the slot's page on the
  // stock read path, which serves data reads from the live page like ROM.
  host->RegisterIO(slot, nullptr, nullptr, nullptr, nullptr);
  host->RegisterCxROM(slot, g_cx_image_a.data());
  return &g_cx_probe;
}

Peripheral_t g_cx_probe_peripheral = {LINAPPLE_ABI_VERSION,
                                      "test.cx_probe",
                                      "CxProbe",
                                      "Swaps its Cx ROM page at run time",
                                      "LinApple Contributors",
                                      "1.0.0",
                                      PERIPHERAL_MASK_EXPANSION,
                                      -1,
                                      cx_probe_init,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr};

auto cx_page_base(int slot) -> uint16_t {
  return static_cast<uint16_t>(0xC000 + (slot << 8));
}

// Two single steps from the top of the slot's page: SEC, then the BCS whose
// displacement is the byte under test. Returns where the 6502 landed.
auto branch_from_page(int slot) -> uint16_t {
  CpuRegisters_t* regs = cpu_get_registers();
  regs->pc = cx_page_base(slot);
  regs->ps = 0;
  cpu_execute(0);
  cpu_execute(0);
  return regs->pc;
}

// LDA $CnFF executed from RAM, so the byte comes back through the 6502's own
// data read of the page rather than from the test poking at the store.
constexpr uint16_t scratch_program = 0x0300;

auto read_page_tail(int slot) -> uint8_t {
  const std::array<uint8_t, 3> lda = {opcode_lda_abs, 0xFF,
                                      static_cast<uint8_t>(0xC0 + slot)};
  TestFixtures::ScopedCore_t::poke(scratch_program, lda);
  CpuRegisters_t* regs = cpu_get_registers();
  regs->pc = scratch_program;
  regs->a = 0;
  cpu_execute(0);
  return regs->a;
}

constexpr uint16_t sw_intcxrom_on = 0xC007;
constexpr uint16_t sw_intcxrom_off = 0xC006;
constexpr uint16_t sw_slotc3rom_on = 0xC00B;

}  // namespace

TEST_CASE(
    "Peripheral ABI: A card's Cx ROM page can change while the machine runs") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  constexpr int slot = 1;
  REQUIRE(peripheral_register(&g_cx_probe_peripheral, slot) == 0);
  REQUIRE(g_cx_probe.host != nullptr);
  linapple_reset_hard();

  CHECK(branch_from_page(slot) == 0xC113);
  CHECK(read_page_tail(slot) == image_a_tail);

  g_cx_probe.host->RegisterCxROM(slot, g_cx_image_b.data());
  CHECK(branch_from_page(slot) == 0xC123);
  CHECK(read_page_tail(slot) == image_b_tail);

  g_cx_probe.host->RegisterCxROM(slot, g_cx_image_a.data());
  CHECK(branch_from_page(slot) == 0xC113);
  CHECK(read_page_tail(slot) == image_a_tail);

  SUBCASE(
      "under INTCXROM the internal ROM stays in view until it is released") {
    io_map_dispatch(0, sw_intcxrom_on, 1, 0, 0);
    const uint8_t internal_first = mem[cx_page_base(slot)];
    const uint8_t internal_tail = mem[cx_page_base(slot) + 0xFF];
    CHECK(internal_first != opcode_sec);

    g_cx_probe.host->RegisterCxROM(slot, g_cx_image_b.data());
    CHECK(mem[cx_page_base(slot)] == internal_first);
    CHECK(mem[cx_page_base(slot) + 0xFF] == internal_tail);
    CHECK(read_page_tail(slot) == internal_tail);

    io_map_dispatch(0, sw_intcxrom_off, 1, 0, 0);
    CHECK(branch_from_page(slot) == 0xC123);
    CHECK(read_page_tail(slot) == image_b_tail);
  }

  peripheral_unregister(slot);
}

TEST_CASE(
    "Peripheral ABI: Slot 3's Cx ROM page changes at run time once SLOTC3ROM "
    "shows it") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);
  constexpr int slot = 3;
  REQUIRE(peripheral_register(&g_cx_probe_peripheral, slot) == 0);
  linapple_reset_hard();

  // Reset leaves the internal 80-column firmware at $C300, so the card's
  // image is registered but not in view.
  const uint8_t internal_tail = mem[cx_page_base(slot) + 0xFF];
  CHECK(mem[cx_page_base(slot)] != opcode_sec);
  g_cx_probe.host->RegisterCxROM(slot, g_cx_image_b.data());
  CHECK(mem[cx_page_base(slot) + 0xFF] == internal_tail);

  io_map_dispatch(0, sw_slotc3rom_on, 1, 0, 0);
  CHECK(branch_from_page(slot) == 0xC323);
  CHECK(read_page_tail(slot) == image_b_tail);

  g_cx_probe.host->RegisterCxROM(slot, g_cx_image_a.data());
  CHECK(branch_from_page(slot) == 0xC313);
  CHECK(read_page_tail(slot) == image_a_tail);

  peripheral_unregister(slot);
}

TEST_CASE(
    "Peripheral ABI: Registering a Cx ROM page with no core is harmless") {
  peripheral_manager_init();
  REQUIRE(peripheral_register(&g_cx_probe_peripheral, 1) == 0);
  g_cx_probe.host->RegisterCxROM(1, g_cx_image_b.data());
  g_cx_probe.host->RegisterCxROM(0, g_cx_image_b.data());
  g_cx_probe.host->RegisterCxROM(8, g_cx_image_b.data());
  g_cx_probe.host->RegisterCxROM(1, nullptr);
  mem_refresh_cx_page(1);
  mem_refresh_cx_page(0);
  mem_refresh_cx_page(8);
  peripheral_manager_shutdown();
}
