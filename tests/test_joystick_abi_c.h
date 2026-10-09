// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/Peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

Peripheral_t* joystick_abi_c_descriptor(void);
size_t joystick_abi_c_state_size(void);
size_t joystick_abi_c_trigger_cycle_offset(void);
size_t joystick_abi_c_trigger_cycle_size(void);
size_t joystick_abi_c_x_pos_offset(void);
size_t joystick_abi_c_y_pos_offset(void);
size_t joystick_abi_c_buttons_offset(void);
size_t joystick_abi_c_trim_x_offset(void);
size_t joystick_abi_c_trim_y_offset(void);
size_t joystick_abi_c_axis_payload_size(void);
uint32_t joystick_abi_c_state_version(void);

#ifdef __cplusplus
}
#endif
