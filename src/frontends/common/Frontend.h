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
auto frontend_dispatch_key_event(uint32_t scancode, uint32_t keycode,
                                 uint32_t mod, bool is_down) -> void;
auto frontend_to_core_key(int key, uint32_t mod) -> LinAppleKey_t;
// Caps Lock as the host reports it: in host mode the Apple's caps follows the
// host's lock state, at each edge of the key and when the window gains focus;
// in emulated mode the key is the Apple's own and a press toggles it.
auto keyboard_sync_host_caps(uint32_t mod) -> void;
auto keyboard_press_caps_lock(uint32_t mod) -> void;
// The host's hands are off the keyboard: the window lost focus, so the
// releases of whatever was held will never arrive.
auto keyboard_release_host_modifiers() -> void;

constexpr int k_window_width = SCREEN_WIDTH;
constexpr int k_window_height = SCREEN_HEIGHT;
