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

enum eIRQSRC {
  IS_6522 = 0,
  IS_SPEECH,
  IS_SSC,
  IS_MOUSE,
  IS_SLOT1,
  IS_SLOT2,
  IS_SLOT3,
  IS_SLOT4,
  IS_SLOT5,
  IS_SLOT6,
  IS_SLOT7
};

typedef PeripheralStatus_t PeripheralStatus;
typedef PeripheralLogLevel_t PeripheralLogLevel;

#ifdef __cplusplus
}
#endif
