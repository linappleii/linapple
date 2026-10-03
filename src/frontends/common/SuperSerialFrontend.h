// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

#include "apple2/peripherals/Peripheral_Internal.h"

struct SuperSerialFrontendSettings_t {
  // A device path, a regular file that must exist, "pty" for a pseudo-terminal
  // created here, "loopback" for a null-modem plug, or empty. A relative path
  // is taken against base_dir; "~" is not expanded.
  std::string port;
  std::string base_dir;
  // Seven ON or OFF tokens each, switch 1 first, as the 1981 manual prints
  // them; empty is the default.
  std::string switches_1;
  std::string switches_2;
};

// The lowest slot that took the switches gets the device; a second card is
// unconnected and reads its own pull-ups.
auto super_serial_frontend_configure(
    const SuperSerialFrontendSettings_t& settings) -> void;

auto super_serial_frontend_sink() -> const ByteSink_t&;

// 0 when no card took the switches.
auto super_serial_frontend_primary_slot() -> int;

// The pseudo-terminal's /dev/pts/N or the resolved device path; empty for
// the loopback plug, no device, and any slot but the primary.
auto super_serial_frontend_device_path(int slot) -> std::string;
