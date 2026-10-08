// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification: C99-compatible public ABI of the Super Serial Card.

#include <stdint.h>

#include "apple2/peripherals/Peripheral_Subsystems.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { SUPER_SERIAL_STATE_VERSION = 1 };

// The two blocks of seven rocker switches (1981 manual pp. 6-8, 22-24): bit k
// is switch k + 1, 1 = ON; bit 7 is no switch and must be 0.
typedef struct {
  uint8_t sw1;
  uint8_t sw2;
} SuperSerialSwitches_t;

// Command and query ids 0x0001 and 0x0002 belonged to a retired host path
// and are never reused.
typedef enum {
  SUPER_SERIAL_CMD_SET_SWITCHES = PERIPHERAL_SUBSYSTEM_SERIAL | 0x0003,
} SuperSerialCmd_t;

// The frame has ridden the slot trailer of every default .aws, so the fields
// before status_latches keep their offsets; rx_count, the three flag bytes,
// rx_buffer and config are dead, written as zeros and read past.
// status_latches: bit 0 PE, 1 FE, 2 OVRN, 3 RDRF, 4 TDR full, 5 transmit
// shifter busy, 6 receive shifter busy; bit 7 must be 0, so an older frame's
// all-zero byte is an idle chip with TDRE set. No cycles are carried: a
// character in flight restarts from its first bit on load.
typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint32_t rx_count;
  uint8_t control_byte;
  uint8_t command_byte;
  uint8_t is_irq_pending;
  uint8_t is_rx_irq_enabled;
  uint8_t is_tx_irq_enabled;
  uint8_t was_tx_written;
  uint8_t rx_buffer[9];
  uint8_t status_latches;
  uint8_t config[24];
  uint8_t receive_data;
  uint8_t transmit_data;
  uint8_t shift_data;
  uint8_t reserved1;
} SuperSerialSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
