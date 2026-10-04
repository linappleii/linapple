// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "SdlBackend.h"

auto joy_frontend_initialize() -> void;
auto joy_frontend_shutdown() -> void;
auto joy_frontend_update() -> void;
auto joy_frontend_update_trim_via_key(SdlKeycode_t virtkey) -> void;
auto joy_frontend_process_key(SdlKeycode_t virtkey, bool extended, bool down,
                              bool autorep) -> bool;
auto joy_frontend_is_mouse_emulation_active() -> bool;
auto joy_frontend_process_mouse_motion(int x, int max_x, int y, int max_y)
    -> void;
auto joy_frontend_process_mouse_button(int button, bool down) -> void;
