// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  peripheral_ok = 0,
  peripheral_error = -1,
  peripheral_incompatible = -2,
  peripheral_busy = -3
} PeripheralStatus_t;

typedef enum {
  log_debug = 0,
  log_info,
  log_warn,
  log_error
} PeripheralLogLevel_t;

enum IrqSrc_t {
  is_6522 = 0,
  is_speech,
  is_ssc,
  is_mouse,
  is_slot1,
  is_slot2,
  is_slot3,
  is_slot4,
  is_slot5,
  is_slot6,
  is_slot7
};

#ifdef __cplusplus
}
#endif
