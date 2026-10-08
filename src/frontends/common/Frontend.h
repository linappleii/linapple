// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "apple2/Video.h"
#include "core/LinAppleCore.h"

struct Configuration;
using AppConfig = Configuration;

auto sys_init() -> int;
auto sys_shutdown() -> void;
auto session_init(AppConfig* config) -> int;
auto session_shutdown() -> void;

auto enter_message_loop() -> void;
auto sys_input() -> void;

auto ds_init() -> bool;
auto ds_shutdown() -> void;
auto single_step(bool is_reinit) -> void;
auto frontend_dispatch_key_event(uint32_t scancode, uint32_t keycode,
                                 uint32_t mod, bool is_down) -> void;
auto frontend_to_core_key(int key, uint32_t mod) -> LinAppleKey;
// In host mode the Apple's caps follows the host's lock state at each edge of
// the key and on focus gain; in emulated mode a press toggles the Apple's own.
auto keyboard_sync_host_caps(uint32_t mod) -> void;
auto keyboard_press_caps_lock(uint32_t mod) -> void;
// On focus loss the releases of whatever was held never arrive.
auto keyboard_release_host_modifiers() -> void;

constexpr int k_window_width = SCREEN_WIDTH;
constexpr int k_window_height = SCREEN_HEIGHT;
