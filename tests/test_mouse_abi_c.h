// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned mouse_abi_c_frame_size(void);
unsigned mouse_abi_c_state_version(void);
unsigned mouse_abi_c_button_payload_size(void);
unsigned mouse_abi_c_move_payload_size(void);
uint32_t mouse_abi_c_set_button_id(void);
uint32_t mouse_abi_c_move_id(void);
uint32_t mouse_abi_c_is_active_query_id(void);
uint32_t mouse_abi_c_position_query_id(void);
unsigned mouse_abi_c_position_report_size(void);

#ifdef __cplusplus
}
#endif
