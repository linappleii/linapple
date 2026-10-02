// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// The one byte sink the frontend installs behind the host interface. The
// bridge opens each slot with a kind, and the dispatcher forwards every call
// for that slot to the printer's or the serial port's implementation, so a
// card never learns which device the frontend put behind its token.
auto host_sink_install() -> void;
