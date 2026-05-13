#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard LinApple Peripheral Status Codes */
typedef enum {
  PERIPHERAL_OK = 0,
  PERIPHERAL_ERROR = -1,
  PERIPHERAL_INCOMPATIBLE = -2,
  PERIPHERAL_BUSY = -3
} PeripheralStatus;

typedef enum {
  LOG_DEBUG = 0,
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR
} PeripheralLogLevel;

#ifdef __cplusplus
}
#endif
