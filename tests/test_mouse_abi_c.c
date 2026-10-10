// SPDX-License-Identifier: GPL-2.0-only
#include "test_mouse_abi_c.h"

#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/mouse/Mouse.h"
#include "apple2/peripherals/mouse/MouseCommands.h"

/* C99 has no static_assert; an array of negative size fails the same way. */
typedef char mouse_frame_is_92_bytes[sizeof(MouseSaveState) == 92 ? 1 : -1];
typedef char
    mouse_version_is_at_0[offsetof(MouseSaveState, version) == 0 ? 1 : -1];
typedef char mouse_struct_size_is_at_4
    [offsetof(MouseSaveState, struct_size) == 4 ? 1 : -1];
typedef char mouse_position_is_at_8[offsetof(MouseSaveState, position_x) == 8
                                        ? 1
                                        : -1];
typedef char
    mouse_clamps_are_at_16[offsetof(MouseSaveState, min_x) == 16 ? 1 : -1];
typedef char mouse_tick_phase_is_at_32
    [offsetof(MouseSaveState, tick_phase) == 32 ? 1 : -1];
typedef char
    mouse_last_read_is_at_40[offsetof(MouseSaveState, read_x) == 40 ? 1 : -1];
typedef char mouse_parser_pos_is_at_48
    [offsetof(MouseSaveState, parser_pos) == 48 ? 1 : -1];
typedef char mouse_parser_length_is_at_52
    [offsetof(MouseSaveState, parser_out_len) == 52 ? 1 : -1];
typedef char mouse_pia_registers_are_at_56
    [offsetof(MouseSaveState, pia_ora) == 56 ? 1 : -1];
typedef char mouse_port_a_in_is_at_62
    [offsetof(MouseSaveState, pia_port_a_in) == 62 ? 1 : -1];
typedef char
    mouse_rate_is_at_64[offsetof(MouseSaveState, rate_50hz) == 64 ? 1 : -1];
typedef char
    mouse_pending_is_at_65[offsetof(MouseSaveState, pending) == 65 ? 1 : -1];
typedef char mouse_irq_asserted_is_at_66
    [offsetof(MouseSaveState, irq_asserted) == 66 ? 1 : -1];
typedef char mouse_parser_in_len_is_at_68
    [offsetof(MouseSaveState, parser_in_len) == 68 ? 1 : -1];
typedef char mouse_parser_reply_pos_is_at_69
    [offsetof(MouseSaveState, parser_reply_pos) == 69 ? 1 : -1];
typedef char mouse_port_a_shadow_is_at_72
    [offsetof(MouseSaveState, pia_port_a_shadow) == 72 ? 1 : -1];
typedef char mouse_port_b_shadow_is_at_73
    [offsetof(MouseSaveState, pia_port_b_shadow) == 73 ? 1 : -1];
typedef char
    mouse_mode_is_at_74[offsetof(MouseSaveState, mode) == 74 ? 1 : -1];
typedef char
    mouse_status_is_at_76[offsetof(MouseSaveState, status) == 76 ? 1 : -1];
typedef char mouse_button_at_last_read_is_at_77
    [offsetof(MouseSaveState, button_at_last_read) == 77 ? 1 : -1];
typedef char
    mouse_button_is_at_79[offsetof(MouseSaveState, button) == 79 ? 1 : -1];
typedef char
    mouse_buffer_is_at_81[offsetof(MouseSaveState, buffer) == 81 ? 1 : -1];
typedef char
    mouse_move_payload_is_8_bytes[sizeof(MouseMovePayload) == 8 ? 1 : -1];
typedef char
    mouse_position_report_is_28_bytes[sizeof(MousePositionReport) == 28 ? 1
                                                                          : -1];
typedef char
    mouse_button_payload_is_4_bytes[sizeof(MouseButtonPayload) == 4 ? 1 : -1];
/* Never called: in the shared build the card is a plugin reached through the
 * registry, so only the declaration is pinned, under sizeof. */
typedef char mouse_accessor_is_declared[sizeof(&mouse_get_descriptor) ? 1 : -1];

unsigned mouse_abi_c_frame_size(void) {
  return (unsigned)sizeof(MouseSaveState);
}

unsigned mouse_abi_c_state_version(void) {
  return (unsigned)MOUSE_STATE_VERSION;
}

unsigned mouse_abi_c_button_payload_size(void) {
  return (unsigned)sizeof(MouseButtonPayload);
}

unsigned mouse_abi_c_move_payload_size(void) {
  return (unsigned)sizeof(MouseMovePayload);
}

uint32_t mouse_abi_c_set_button_id(void) {
  return (uint32_t)mouse_cmd_set_button;
}

uint32_t mouse_abi_c_move_id(void) { return (uint32_t)mouse_cmd_move; }

uint32_t mouse_abi_c_is_active_query_id(void) {
  return (uint32_t)mouse_query_is_active;
}

uint32_t mouse_abi_c_position_query_id(void) {
  return (uint32_t)mouse_query_position;
}

unsigned mouse_abi_c_position_report_size(void) {
  return (unsigned)sizeof(MousePositionReport);
}
