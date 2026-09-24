// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>

#include "apple2/peripherals/Peripheral_Subsystems.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)

typedef enum {
  peripheral_ok = 0,
  peripheral_error = -1,
  peripheral_incompatible = -2,
  peripheral_busy = -3
} PeripheralStatus_t;

typedef enum {
  log_debug = 0,
  log_info,
  log_warn,
  log_error
} PeripheralLogLevel_t;

// What a byte stream leaving a card is for. The host names its destination by
// slot and kind, so two printer cards get two destinations.
typedef enum {
  peripheral_sink_printer = 1,
  peripheral_sink_serial = 2
} PeripheralSinkKind_t;

enum IrqSrc_t {
  is_6522 = 0,
  is_speech,
  is_ssc,
  is_mouse,
  is_slot1,
  is_slot2,
  is_slot3,
  is_slot4,
  is_slot5,
  is_slot6,
  is_slot7
};

// IWYU pragma: begin_exports
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "apple2/peripherals/mockingboard/MockingboardCommands.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
// IWYU pragma: end_exports

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)
