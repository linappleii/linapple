// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

unsigned keyboard_abi_c_frame_size(void);
unsigned keyboard_abi_c_state_version(void);
unsigned keyboard_abi_c_repeat_key_offset(void);
unsigned keyboard_abi_c_latch_offset(void);
unsigned keyboard_abi_c_strobe_offset(void);
unsigned keyboard_abi_c_caps_lock_offset(void);
unsigned keyboard_abi_c_auto_repeat_offset(void);
unsigned keyboard_abi_c_key_event_size(void);

#ifdef __cplusplus
}
#endif
