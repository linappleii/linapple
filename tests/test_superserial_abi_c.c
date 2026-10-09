// SPDX-License-Identifier: GPL-2.0-only
/* A plugin written in C must see the same frame and switch payload. */
#include "test_superserial_abi_c.h"

#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"

/* Declared, never called: in a plugin build it lives inside the shared
 * object. */
struct Peripheral_t* super_serial_get_descriptor(void);

/* C99 has no static_assert; an array of negative size fails the same way. */
typedef char superserial_frame_is_56_bytes[sizeof(SuperSerialSaveState_t) == 56
                                               ? 1
                                               : -1];
typedef char superserial_control_is_at_12
    [offsetof(SuperSerialSaveState_t, control_byte) == 12 ? 1 : -1];
typedef char superserial_command_is_at_13
    [offsetof(SuperSerialSaveState_t, command_byte) == 13 ? 1 : -1];
typedef char superserial_irq_is_at_14
    [offsetof(SuperSerialSaveState_t, is_irq_pending) == 14 ? 1 : -1];
typedef char superserial_status_latches_is_at_27
    [offsetof(SuperSerialSaveState_t, status_latches) == 27 ? 1 : -1];
typedef char superserial_config_is_at_28
    [offsetof(SuperSerialSaveState_t, config) == 28 ? 1 : -1];
typedef char superserial_config_is_24_bytes
    [sizeof(((SuperSerialSaveState_t*)0)->config) == 24 ? 1 : -1];
typedef char superserial_receive_data_is_at_52
    [offsetof(SuperSerialSaveState_t, receive_data) == 52 ? 1 : -1];
typedef char superserial_transmit_data_is_at_53
    [offsetof(SuperSerialSaveState_t, transmit_data) == 53 ? 1 : -1];
typedef char superserial_shift_data_is_at_54
    [offsetof(SuperSerialSaveState_t, shift_data) == 54 ? 1 : -1];
typedef char superserial_switches_are_2_bytes[sizeof(SuperSerialSwitches_t) == 2
                                                  ? 1
                                                  : -1];

unsigned superserial_abi_c_frame_size(void) {
  return (unsigned)sizeof(SuperSerialSaveState_t);
}

unsigned superserial_abi_c_state_version(void) {
  return (unsigned)SUPER_SERIAL_STATE_VERSION;
}

unsigned superserial_abi_c_switches_size(void) {
  return (unsigned)sizeof(SuperSerialSwitches_t);
}

uint32_t superserial_abi_c_set_switches_id(void) {
  return (uint32_t)SUPER_SERIAL_CMD_SET_SWITCHES;
}
