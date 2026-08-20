// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>

#include "core/Peripheral.h"

constexpr size_t CLOCK_LATCHES_COUNT = 10;

constexpr int LATCH_MONTH = 0;
constexpr int LATCH_WEEKDAY = 2;
constexpr int LATCH_DAY = 4;
constexpr int LATCH_HOUR = 6;
constexpr int LATCH_MINUTE = 8;

auto clock_get_descriptor() -> Peripheral_t*;
