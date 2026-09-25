// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class)
// Justification: This header defines the C99-compatible public ABI for the
// parallel printer card.

#include <stdint.h>

#include "apple2/peripherals/Peripheral_Subsystems.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { PRINTER_STATE_VERSION = 1 };

typedef enum {
  PRINTER_CMD_SET_ONLINE = PERIPHERAL_SUBSYSTEM_PRINTER | 0x0101,
  PRINTER_CMD_RESET_STATS = PERIPHERAL_SUBSYSTEM_PRINTER | 0x0102
} PrinterCmd_e;

typedef enum {
  PRINTER_QUERY_STATUS = PERIPHERAL_SUBSYSTEM_PRINTER | 0x0100
} PrinterQuery_e;

typedef struct {
  uint8_t online;
  uint8_t reserved[3];
} PrinterOnlineCmd_t;

typedef struct {
  uint64_t total_chars_printed;
  uint8_t is_online;
  uint8_t is_busy;
  uint8_t last_char;
  uint8_t reserved;
  uint32_t padding;
} PrinterStatusQuery_t;

typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint64_t total_chars_printed;
  uint32_t busy_cycles;
  uint8_t data_latch;
  uint8_t status_latch;
  uint8_t is_online;
  uint8_t is_busy;
} PrinterSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class)
