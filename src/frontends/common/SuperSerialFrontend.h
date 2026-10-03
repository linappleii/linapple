// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

#include "apple2/peripherals/Peripheral_Internal.h"

// What the frontend's serial port is told once per run, after the
// configuration has been read, the directories resolved and the cards
// registered.
struct SuperSerialFrontendSettings_t {
  // The host device behind the card's RS-232 line: a device path, a regular
  // file that must already exist, "pty" for a pseudo-terminal created here,
  // "loopback" for a null-modem plug, or empty for no device. A relative
  // path is taken against base_dir; "~" is not expanded.
  std::string port;
  std::string base_dir;
  // The two blocks of DIP switches as the 1981 manual prints them: seven ON
  // or OFF tokens each, switch 1 first. An empty row is the default.
  std::string switches_1;
  std::string switches_2;
};

// Sends the switch rows to every Super Serial Card in the machine, makes the
// lowest slot that took them the primary serial slot, and puts the device
// behind that slot's token. A second card is unconnected and reads its own
// pull-ups.
auto super_serial_frontend_configure(
    const SuperSerialFrontendSettings_t& settings) -> void;

// The serial side of the frontend's byte sink: the RS-232 line behind a
// Super Serial Card's token, forwarded here by the host sink for every slot
// opened as a serial line.
auto super_serial_frontend_sink() -> const ByteSink_t&;

// The slot whose token the device is behind, or 0 when no card took the
// switches.
auto super_serial_frontend_primary_slot() -> int;

// The path a peer connects to: the pseudo-terminal's /dev/pts/N, or the
// resolved device path. Empty for the loopback plug, for no device and for
// any slot but the primary.
auto super_serial_frontend_device_path(int slot) -> std::string;
