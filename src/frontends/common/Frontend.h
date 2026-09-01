// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "frontends/common/AppConfig.h"

// Lifecycle
auto sys_init() -> int;
void SysShutdown();
auto session_init(AppConfig_t* config) -> int;
void SessionShutdown();

void ContinueExecution(uint32_t cycles);
void CpuTestHeadless(const char* test_file);

// Entry point helpers (implemented in Frontend)
void enter_message_loop();
void sys_input();
void Sys_Think();
void Sys_Draw();

// Public Frontend helpers (Keyboard translation, etc)
auto ds_init() -> bool;
void ds_shutdown();
void SingleStep(bool is_reinit);
void Linapple_KeyboardThink(uint32_t cycles);
void Frontend_UpdateKeyboardMapping();
auto keyboard_get_caps_mode() -> int;
auto keyboard_set_caps_mode(int mode) -> void;
void Frontend_DispatchKeyEvent(uint32_t scancode, uint32_t keycode,
                               uint32_t mod, bool is_down);
LinAppleKey frontend_to_core_key(int key, uint32_t mod);

// Constants
constexpr int window_width = SCREEN_WIDTH;
constexpr int window_height = SCREEN_HEIGHT;
