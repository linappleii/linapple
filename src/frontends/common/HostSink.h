// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// The one byte sink behind the host interface: calls for a slot go to the
// printer's or the serial port's implementation by the kind it was opened as.
auto host_sink_install() -> void;
