// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class)
// Justification: This header defines the C99-compatible public ABI for the
// clock card.

#include <stdint.h>

#include "apple2/peripherals/Peripheral_Subsystems.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { CLOCKCARD_STATE_VERSION = 1 };

typedef enum {
  clockcard_cmd_set_epoch = PERIPHERAL_SUBSYSTEM_CLOCK | 0x0001,
  clockcard_cmd_clear_epoch = PERIPHERAL_SUBSYSTEM_CLOCK | 0x0002
} ClockCardCmd_e;

typedef enum {
  clockcard_query_epoch = PERIPHERAL_SUBSYSTEM_CLOCK | 0x0100,
  clockcard_query_time = PERIPHERAL_SUBSYSTEM_CLOCK | 0x0101
} ClockCardQuery_e;

typedef struct {
  uint64_t epoch;
} ClockCardSetEpochPayload_t;

typedef struct {
  uint64_t epoch;
  uint8_t is_fixed;
} ClockCardEpochQuery_t;

typedef struct {
  uint8_t month;
  uint8_t day_of_week;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
} ClockCardTimeQuery_t;

typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint64_t fixed_epoch;
  uint8_t latches[10];
  uint8_t use_fixed_epoch;
  uint8_t reserved[5];
} ClockCardSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class)
