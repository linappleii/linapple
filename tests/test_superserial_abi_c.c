// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"

/* C99 has no static_assert; an array of negative size fails the same way.
 * The frame has been written by the slot trailer of every default .aws, so
 * its size and the offsets of its named bytes are pinned here as C sees
 * them. */
typedef char superserial_frame_is_56_bytes[sizeof(SuperSerialSaveState_t) == 56
                                               ? 1
                                               : -1];
typedef char superserial_reserved0_is_at_27
    [offsetof(SuperSerialSaveState_t, reserved0) == 27 ? 1 : -1];
typedef char superserial_config_is_at_28
    [offsetof(SuperSerialSaveState_t, config) == 28 ? 1 : -1];
typedef char superserial_reserved1_is_at_52
    [offsetof(SuperSerialSaveState_t, reserved1) == 52 ? 1 : -1];

unsigned superserial_abi_c_frame_size(void) {
  return (unsigned)sizeof(SuperSerialSaveState_t);
}

unsigned superserial_abi_c_state_version(void) {
  return (unsigned)SUPER_SERIAL_STATE_VERSION;
}
