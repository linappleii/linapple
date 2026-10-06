// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/JoystickConfig.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Registry.h"

namespace {

struct JoystickTypeInfo_t {
  JoystickDevice_t device;
  JoystickMode_t mode;
};

constexpr std::array<JoystickTypeInfo_t, joystick_config_type_count>
    k_joy_info = {{{joystick_device_none, joystick_mode_none},
                   {joystick_device_joystick, joystick_mode_standard},
                   {joystick_device_keyboard, joystick_mode_standard},
                   {joystick_device_keyboard, joystick_mode_centering},
                   {joystick_device_mouse, joystick_mode_standard}}};

auto type_info(uint32_t type) -> const JoystickTypeInfo_t& {
  return k_joy_info.at(type < joystick_config_type_count ? type : 0);
}

auto configured_type(const char* key) -> uint32_t {
  uint32_t type = 0;
  if (!load(key, &type) || type >= joystick_config_type_count) {
    return 0;
  }
  return type;
}

}  // namespace

auto joystick_config_device(uint32_t type) -> JoystickDevice_t {
  return type_info(type).device;
}

auto joystick_config_mode(uint32_t type) -> JoystickMode_t {
  return type_info(type).mode;
}

// Joystick 0's first button is PB0 and its second PB1, the second only while
// no device sits on joystick 1; joystick 1's one button is PB2 and PB1
// together. One connector owner per line, so a device's poll never
// overwrites a button the other device just pressed.
auto joystick_config_button_lines(size_t joy_num, int button, uint32_t type0,
                                  uint32_t type1) -> uint8_t {
  (void)type0;
  if (joy_num != 0) {
    return button == 0
               ? static_cast<uint8_t>(joystick_line_pb2 | joystick_line_pb1)
               : 0;
  }
  if (button == 0) {
    return joystick_line_pb0;
  }
  if (button == 1 && joystick_config_device(type1) == joystick_device_none) {
    return joystick_line_pb1;
  }
  return 0;
}

auto joystick_config_pulldown_mask(uint32_t type0, uint32_t type1) -> uint8_t {
  const std::array<uint32_t, 2> types = {type0, type1};
  uint8_t mask = 0;
  for (size_t joy_num = 0; joy_num < types.size(); ++joy_num) {
    if (joystick_config_device(types.at(joy_num)) == joystick_device_none) {
      continue;
    }
    mask |= joystick_config_button_lines(joy_num, 0, type0, type1);
    mask |= joystick_config_button_lines(joy_num, 1, type0, type1);
  }
  return mask;
}

auto joystick_config_pulldown_mask() -> uint8_t {
  return joystick_config_pulldown_mask(configured_type(REGVALUE_JOY_TYPE1),
                                       configured_type(REGVALUE_JOY_TYPE2));
}

auto joystick_config_shift_key_mod() -> bool {
  uint32_t jumper = 0;
  return load(REGVALUE_SHIFT_KEY_MOD, &jumper) && jumper != 0;
}
