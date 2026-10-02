// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/peripherals/Peripheral_Internal.h"

// The serial side of the frontend's byte sink: the RS-232 line behind a
// Super Serial Card's token, forwarded here by the host sink for every slot
// opened as a serial line. With no device attached the line drops what it is
// given, has no byte to hand back and reports no modem inputs, leaving the
// card to read its own pull-ups.
auto super_serial_frontend_sink() -> const ByteSink_t&;
