// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PRINTER_STATE_VERSION = 1 };

typedef enum {
  PRINTER_CMD_SET_ONLINE = 0x0101,
  PRINTER_CMD_RESET_STATS = 0x0102
} PrinterCmd_e;

typedef enum { PRINTER_QUERY_STATUS = 0x0100 } PrinterQuery_e;

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
} SsCardPrinter_t;

#ifdef __cplusplus
}
#endif
// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)
