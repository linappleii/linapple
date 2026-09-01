// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "frontends/common/AppConfig.h"

// Lifecycle
auto sys_init() -> int;
auto sys_shutdown() -> void;
auto session_init(AppConfig_t* config) -> int;
auto session_shutdown() -> void;

// Entry point helpers (implemented in Frontend)
auto enter_message_loop() -> void;
auto sys_input() -> void;

// Public Frontend helpers (Keyboard translation, etc)
auto ds_init() -> bool;
auto ds_shutdown() -> void;
auto single_step(bool is_reinit) -> void;
auto frontend_update_keyboard_mapping() -> void;
auto keyboard_get_caps_mode() -> int;
auto keyboard_set_caps_mode(int mode) -> void;
auto frontend_dispatch_key_event(uint32_t scancode, uint32_t keycode,
                                 uint32_t mod, bool is_down) -> void;
auto frontend_to_core_key(int key, uint32_t mod) -> LinAppleKey;

// Constants
constexpr int window_width = SCREEN_WIDTH;
constexpr int window_height = SCREEN_HEIGHT;
