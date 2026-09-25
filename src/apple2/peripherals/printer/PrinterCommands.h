// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class)
// Justification: This header defines the C99-compatible public ABI for the
// parallel printer card.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PRINTER_STATE_VERSION = 1 };

// The byte on the card's data lines is its only architectural state: the
// A2B0002 has one 8-bit register, no status port and no counter.
// total_chars_printed, busy_cycles, status_latch, is_online and is_busy once
// carried the host's bookkeeping and a busy model the card never had; the
// card writes them as zeros and reads past them. The layout stays as it is so
// every frame ever written loads.
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
