// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned superserial_abi_c_frame_size(void);
unsigned superserial_abi_c_state_version(void);
unsigned superserial_abi_c_switches_size(void);
uint32_t superserial_abi_c_set_switches_id(void);

#ifdef __cplusplus
}
#endif
