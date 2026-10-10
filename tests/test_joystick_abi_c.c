/* C99 view of the game port's public headers: the descriptor accessor, the
 * state frame's layout and the payload sizes as a C consumer sees them. */
#include "test_joystick_abi_c.h"

#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"

struct Peripheral* joystick_abi_c_descriptor(void) {
  return joystick_get_descriptor();
}

size_t joystick_abi_c_state_size(void) {
  JoystickSaveState state;
  state.version = JOYSTICK_STATE_VERSION;
  state.struct_size = (uint32_t)sizeof(JoystickSaveState);
  return sizeof(state);
}

size_t joystick_abi_c_trigger_cycle_offset(void) {
  return offsetof(JoystickSaveState, trigger_cycle);
}

size_t joystick_abi_c_trigger_cycle_size(void) {
  JoystickSaveState state;
  return sizeof(state.trigger_cycle);
}

size_t joystick_abi_c_x_pos_offset(void) {
  return offsetof(JoystickSaveState, x_pos);
}

size_t joystick_abi_c_y_pos_offset(void) {
  return offsetof(JoystickSaveState, y_pos);
}

size_t joystick_abi_c_buttons_offset(void) {
  return offsetof(JoystickSaveState, buttons);
}

size_t joystick_abi_c_trim_x_offset(void) {
  return offsetof(JoystickSaveState, trim_x);
}

size_t joystick_abi_c_trim_y_offset(void) {
  return offsetof(JoystickSaveState, trim_y);
}

size_t joystick_abi_c_axis_payload_size(void) {
  return sizeof(JoystickAxisPayload);
}

uint32_t joystick_abi_c_state_version(void) { return JOYSTICK_STATE_VERSION; }
