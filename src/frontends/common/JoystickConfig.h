// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

// Behind a configured Joystick type: 0 none, 1 gamepad, 2 keypad, 3 keypad
// self-centring, 4 mouse.
enum JoystickDevice : int {
  joystick_device_none = 0,
  joystick_device_joystick = 1,
  joystick_device_keyboard = 2,
  joystick_device_mouse = 3,
};

enum JoystickMode : int {
  joystick_mode_none = 0,
  joystick_mode_standard = 1,
  joystick_mode_centering = 2,
  joystick_mode_smooth = 3,
};

constexpr uint32_t joystick_config_type_count = 5;

constexpr uint8_t joystick_line_pb0 = 0x01;
constexpr uint8_t joystick_line_pb1 = 0x02;
constexpr uint8_t joystick_line_pb2 = 0x04;

auto joystick_config_device(uint32_t type) -> JoystickDevice;
auto joystick_config_mode(uint32_t type) -> JoystickMode;

auto joystick_config_button_lines(size_t joy_num, int button, uint32_t type0,
                                  uint32_t type1) -> uint8_t;

// A controller plug carries a 560 ohm resistor on each line it has a button
// for (Sather, Understanding the Apple II, 7-9 and 7-11); a line no device
// drives is left open.
auto joystick_config_pulldown_mask(uint32_t type0, uint32_t type1) -> uint8_t;

// From the configuration alone, before a frontend has opened the devices.
auto joystick_config_pulldown_mask() -> uint8_t;

auto joystick_config_shift_key_mod() -> bool;
