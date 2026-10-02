// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>

#include "HeadlessHarness.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

using TestFixtures::ScopedTestConfig_t;

// A bench card that opens the kind of token it is told to and keeps the host
// it was handed, so a case can drive the frontend's sink through the same
// members a real card uses.
struct TokenProbe_t {
  HostInterface_t* host = nullptr;
  void* token = nullptr;
  int slot = 0;
};

TokenProbe_t g_serial_probe;
TokenProbe_t g_printer_probe;

auto serial_probe_init(int slot, HostInterface_t* host) -> void* {
  g_serial_probe = TokenProbe_t();
  g_serial_probe.host = host;
  g_serial_probe.slot = slot;
  g_serial_probe.token =
      host->SinkOpen(&g_serial_probe, slot, peripheral_sink_serial);
  return &g_serial_probe;
}

auto printer_probe_init(int slot, HostInterface_t* host) -> void* {
  g_printer_probe = TokenProbe_t();
  g_printer_probe.host = host;
  g_printer_probe.slot = slot;
  g_printer_probe.token =
      host->SinkOpen(&g_printer_probe, slot, peripheral_sink_printer);
  return &g_printer_probe;
}

auto probe_shutdown(void* instance) -> void {
  auto* probe = static_cast<TokenProbe_t*>(instance);
  probe->host->SinkClose(probe->token);
}

Peripheral_t g_serial_probe_card = {LINAPPLE_ABI_VERSION,
                                    "test.serial_token_probe",
                                    "SerialTokenProbe",
                                    "Opens a serial token at init",
                                    "LinApple Contributors",
                                    "1.0.0",
                                    PERIPHERAL_MASK_EXPANSION,
                                    -1,
                                    serial_probe_init,
                                    nullptr,
                                    probe_shutdown,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr};

Peripheral_t g_printer_probe_card = {LINAPPLE_ABI_VERSION,
                                     "test.printer_token_probe",
                                     "PrinterTokenProbe",
                                     "Opens a printer token at init",
                                     "LinApple Contributors",
                                     "1.0.0",
                                     PERIPHERAL_MASK_EXPANSION,
                                     -1,
                                     printer_probe_init,
                                     nullptr,
                                     probe_shutdown,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr};

constexpr int serial_probe_slot = 3;
constexpr int printer_probe_slot = 5;

}  // namespace

TEST_CASE(
    "Serial Frontend: the frontend's sink is installed, and a serial token "
    "opened through it reads no byte and reports no lines") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  REQUIRE(peripheral_register(&g_serial_probe_card, serial_probe_slot) == 0);
  REQUIRE(peripheral_register(&g_printer_probe_card, printer_probe_slot) == 0);
  REQUIRE(g_serial_probe.token != nullptr);
  REQUIRE(g_printer_probe.token != nullptr);
  HostInterface_t* host = g_serial_probe.host;

  // The printer side of the installed sink is live: a printer slot is ready
  // the moment it is opened, so the dispatcher is in and forwarding by kind.
  CHECK(host->SinkReady(g_printer_probe.token));

  // The serial side has no device behind it yet.
  uint8_t byte = 0x5A;
  CHECK(host->SinkRead(g_serial_probe.token, &byte) == false);
  CHECK(byte == 0x5A);
  uint8_t lines = 0xA5;
  CHECK(host->SinkGetLines(g_serial_probe.token, &lines) == false);
  CHECK(lines == 0xA5);
  CHECK(host->SinkReady(g_serial_probe.token) == false);
  host->SinkWrite(g_serial_probe.token, 0xC8);
  const PeripheralSerialLine_t line = {9600, 8, 0, 2, 1, 1, 0, {0, 0}};
  host->SinkSetLine(g_serial_probe.token, &line);
  CHECK(host->SinkRead(g_serial_probe.token, &byte) == false);
  CHECK(byte == 0x5A);

  peripheral_unregister(serial_probe_slot);
  peripheral_unregister(printer_probe_slot);
}
