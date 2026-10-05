// SPDX-License-Identifier: GPL-2.0-only
/* A plugin written in C must see the same frame and payloads. */
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/mouse/MouseCommands.h"

/* C99 has no static_assert; an array of negative size fails the same way. */
typedef char mouse_frame_is_92_bytes[sizeof(MouseSaveState_t) == 92 ? 1 : -1];
typedef char
    mouse_version_is_at_0[offsetof(MouseSaveState_t, version) == 0 ? 1 : -1];
typedef char mouse_struct_size_is_at_4
    [offsetof(MouseSaveState_t, struct_size) == 4 ? 1 : -1];
typedef char mouse_position_is_at_8[offsetof(MouseSaveState_t, internal_x) == 8
                                        ? 1
                                        : -1];
typedef char
    mouse_clamps_are_at_16[offsetof(MouseSaveState_t, min_x) == 16 ? 1 : -1];
typedef char
    mouse_last_read_is_at_40[offsetof(MouseSaveState_t, pos_x) == 40 ? 1 : -1];
typedef char mouse_parser_pos_is_at_48
    [offsetof(MouseSaveState_t, buffer_pos) == 48 ? 1 : -1];
typedef char mouse_parser_length_is_at_52
    [offsetof(MouseSaveState_t, data_len) == 52 ? 1 : -1];
typedef char mouse_pia_registers_are_at_56
    [offsetof(MouseSaveState_t, pia_ora) == 56 ? 1 : -1];
typedef char mouse_port_a_in_is_at_62
    [offsetof(MouseSaveState_t, pia_port_a_in) == 62 ? 1 : -1];
typedef char mouse_port_a_shadow_is_at_72
    [offsetof(MouseSaveState_t, pia_port_a_shadow) == 72 ? 1 : -1];
typedef char mouse_port_b_shadow_is_at_73
    [offsetof(MouseSaveState_t, pia_port_b_shadow) == 73 ? 1 : -1];
typedef char
    mouse_mode_is_at_74[offsetof(MouseSaveState_t, mode) == 74 ? 1 : -1];
typedef char
    mouse_status_is_at_76[offsetof(MouseSaveState_t, status_state) == 76 ? 1
                                                                         : -1];
typedef char mouse_button_at_last_read_is_at_77
    [offsetof(MouseSaveState_t, btn0_prev) == 77 ? 1 : -1];
typedef char
    mouse_button_is_at_79[offsetof(MouseSaveState_t, buttons) == 79 ? 1 : -1];
typedef char
    mouse_buffer_is_at_81[offsetof(MouseSaveState_t, buffer) == 81 ? 1 : -1];
typedef char
    mouse_button_payload_is_4_bytes[sizeof(MouseButtonPayload_t) == 4 ? 1 : -1];

unsigned mouse_abi_c_frame_size(void) {
  return (unsigned)sizeof(MouseSaveState_t);
}

unsigned mouse_abi_c_state_version(void) {
  return (unsigned)MOUSE_STATE_VERSION;
}

unsigned mouse_abi_c_button_payload_size(void) {
  return (unsigned)sizeof(MouseButtonPayload_t);
}

uint32_t mouse_abi_c_set_button_id(void) {
  return (uint32_t)mouse_cmd_set_button;
}

uint32_t mouse_abi_c_is_active_query_id(void) {
  return (uint32_t)mouse_query_is_active;
}
