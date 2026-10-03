// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "apple2/Video.h"
#include "core/LinAppleCore.h"

struct Configuration_t;
using AppConfig_t = Configuration_t;

auto sys_init() -> int;
auto sys_shutdown() -> void;
auto session_init(AppConfig_t* config) -> int;
auto session_shutdown() -> void;

auto enter_message_loop() -> void;
auto sys_input() -> void;

auto ds_init() -> bool;
auto ds_shutdown() -> void;
auto single_step(bool is_reinit) -> void;
auto frontend_update_keyboard_mapping() -> void;
auto keyboard_get_caps_mode() -> int;
auto keyboard_set_caps_mode(int mode) -> void;
auto frontend_dispatch_key_event(uint32_t scancode, uint32_t keycode,
                                 uint32_t mod, bool is_down) -> void;
auto frontend_to_core_key(int key, uint32_t mod) -> LinAppleKey_t;

constexpr int k_window_width = SCREEN_WIDTH;
constexpr int k_window_height = SCREEN_HEIGHT;
