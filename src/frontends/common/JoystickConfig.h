// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

// The host device behind each Apple joystick, in the numbering Joystick 0
// and Joystick 1 use in the configuration: 0 none, 1 a gamepad, 2 the keypad,
// 3 the keypad self-centring, 4 the mouse.
enum JoystickDevice_t : int {
  joystick_device_none = 0,
  joystick_device_joystick = 1,
  joystick_device_keyboard = 2,
  joystick_device_mouse = 3
};

enum JoystickMode_t : int {
  joystick_mode_none = 0,
  joystick_mode_standard = 1,
  joystick_mode_centering = 2,
  joystick_mode_smooth = 3
};

constexpr uint32_t joystick_config_type_count = 5;

constexpr uint8_t joystick_line_pb0 = 0x01;
constexpr uint8_t joystick_line_pb1 = 0x02;
constexpr uint8_t joystick_line_pb2 = 0x04;

auto joystick_config_device(uint32_t type) -> JoystickDevice_t;
auto joystick_config_mode(uint32_t type) -> JoystickMode_t;

// The switch lines a button of one Apple joystick drives, given the device
// type of both.
auto joystick_config_button_lines(size_t joy_num, int button, uint32_t type0,
                                  uint32_t type1) -> uint8_t;

// The lines the present devices' buttons pull down: a controller plug carries
// a 560 ohm resistor on each line it has a button for (Sather, Understanding
// the Apple II, 7-9 and 7-11), so a line no device drives is left open.
auto joystick_config_pulldown_mask(uint32_t type0, uint32_t type1) -> uint8_t;

// The same from Joystick 0 and Joystick 1 as configured, before a frontend
// has looked for the devices.
auto joystick_config_pulldown_mask() -> uint8_t;

// The Shift-key mod jumper as configured.
auto joystick_config_shift_key_mod() -> bool;
