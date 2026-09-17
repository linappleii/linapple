// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class)
// Justification: This header defines the C99-compatible public ABI for the
// Clock subsystem.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { CLOCK_STATE_VERSION = 1 };

typedef enum {
  clock_cmd_set_epoch = 0x0001,
  clock_cmd_clear_epoch = 0x0002
} ClockCmd_e;

typedef enum {
  clock_query_get_epoch = 0x0100,
  clock_query_get_time = 0x0101
} ClockQuery_e;

typedef struct {
  uint64_t epoch;
} ClockSetEpochPayload_t;

typedef struct {
  uint64_t epoch;
  uint8_t is_fixed;
} ClockEpochQuery_t;

typedef struct {
  uint8_t month;
  uint8_t day_of_week;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
} ClockTimeQuery_t;

typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint64_t fixed_epoch;
  uint8_t latches[10];
  uint8_t use_fixed_epoch;
  uint8_t reserved[5];
} ClockSaveState_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type, cppcoreguidelines-use-enum-class)
