// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Peripheral.h"

constexpr size_t CLOCK_LATCHES_COUNT = 10;

constexpr int LATCH_MONTH = 0;
constexpr int LATCH_WEEKDAY = 2;
constexpr int LATCH_DAY = 4;
constexpr int LATCH_HOUR = 6;
constexpr int LATCH_MINUTE = 8;

typedef enum {
  clock_cmd_set_epoch =
      0x0001, /**< data: uint64_t or uint32_t epoch timestamp */
  clock_cmd_clear_epoch = 0x0002, /**< data: none */
} ClockCmd_t;

auto clock_get_descriptor() -> Peripheral_t*;
