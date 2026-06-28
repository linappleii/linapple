// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PERIPHERAL_OK = 0,
  PERIPHERAL_ERROR = -1,
  PERIPHERAL_INCOMPATIBLE = -2,
  PERIPHERAL_BUSY = -3
} PeripheralStatus_t;

typedef enum {
  LOG_DEBUG = 0,
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR
} PeripheralLogLevel_t;

typedef PeripheralStatus_t PeripheralStatus;
typedef PeripheralLogLevel_t PeripheralLogLevel;

#ifdef __cplusplus
}
#endif
