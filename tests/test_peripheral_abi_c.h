// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Peripheral_t g_test_c_peripheral;

int test_c_peripheral_read_clock(HostInterface_t* host, int64_t* unix_seconds,
                                 uint8_t* weekday);

int test_c_peripheral_sink_write(HostInterface_t* host, int slot,
                                 uint8_t byte);

#ifdef __cplusplus
}
#endif
