// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using)
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRINTER_STATE_VERSION 1

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
// NOLINTEND(modernize-deprecated-headers, modernize-use-using)
