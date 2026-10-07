// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <stdint.h>

// Included to prove the header is C99; nothing in it is called.
#include "apple2/peripherals/keyboard/Keyboard.h"  // NOLINT(misc-include-cleaner)
#include "apple2/peripherals/keyboard/KeyboardCommands.h"

/* C99 has no static_assert; an array of negative size fails the same way. */
typedef char
    keyboard_frame_is_552_bytes[sizeof(KeyboardSaveState_t) == 552 ? 1 : -1];
typedef char keyboard_repeat_key_is_at_12
    [offsetof(KeyboardSaveState_t, repeat_key) == 12 ? 1 : -1];
typedef char keyboard_latch_is_at_24
    [offsetof(KeyboardSaveState_t, current_latch) == 24 ? 1 : -1];
typedef char
    keyboard_strobe_is_at_25[offsetof(KeyboardSaveState_t, strobe) == 25 ? 1
                                                                         : -1];
typedef char keyboard_caps_lock_is_at_31
    [offsetof(KeyboardSaveState_t, caps_lock) == 31 ? 1 : -1];
typedef char keyboard_auto_repeat_is_at_35
    [offsetof(KeyboardSaveState_t, auto_repeat_enabled) == 35 ? 1 : -1];
typedef char
    keyboard_key_event_is_12_bytes[sizeof(KeyboardKeyEvent_t) == 12 ? 1 : -1];
typedef char keyboard_key_event_host_key_is_at_0
    [offsetof(KeyboardKeyEvent_t, host_key) == 0 ? 1 : -1];
typedef char keyboard_key_event_code_is_at_4
    [offsetof(KeyboardKeyEvent_t, apple_code) == 4 ? 1 : -1];
typedef char keyboard_key_event_down_is_at_5
    [offsetof(KeyboardKeyEvent_t, is_down) == 5 ? 1 : -1];

unsigned keyboard_abi_c_frame_size(void) {
  return (unsigned)sizeof(KeyboardSaveState_t);
}

unsigned keyboard_abi_c_state_version(void) {
  return (unsigned)KEYBOARD_STATE_VERSION;
}

unsigned keyboard_abi_c_repeat_key_offset(void) {
  return (unsigned)offsetof(KeyboardSaveState_t, repeat_key);
}

unsigned keyboard_abi_c_latch_offset(void) {
  return (unsigned)offsetof(KeyboardSaveState_t, current_latch);
}

unsigned keyboard_abi_c_strobe_offset(void) {
  return (unsigned)offsetof(KeyboardSaveState_t, strobe);
}

unsigned keyboard_abi_c_caps_lock_offset(void) {
  return (unsigned)offsetof(KeyboardSaveState_t, caps_lock);
}

unsigned keyboard_abi_c_auto_repeat_offset(void) {
  return (unsigned)offsetof(KeyboardSaveState_t, auto_repeat_enabled);
}

unsigned keyboard_abi_c_key_event_size(void) {
  return (unsigned)sizeof(KeyboardKeyEvent_t);
}
