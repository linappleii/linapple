// SPDX-License-Identifier: GPL-2.0-only
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
