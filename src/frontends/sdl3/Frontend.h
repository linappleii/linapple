#pragma once

#include <cstdint>

#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "frontends/common/AppConfig.h"

// Lifecycle
auto sys_init() -> int;
void SysShutdown();
auto session_init(AppConfig* config) -> int;
void SessionShutdown();

void ContinueExecution(uint32_t dwCycles);
void CpuTestHeadless(const char* szTestFile);

// Entry point helpers (implemented in Frontend)
void EnterMessageLoop();
void Sys_Input();
void Sys_Think();
void Sys_Draw();

// Public Frontend helpers (Keyboard translation, etc)
auto ds_init() -> bool;
void DSShutdown();
void SingleStep(bool bReinit);
void Linapple_KeyboardThink(uint32_t dwCycles);
void Frontend_UpdateKeyboardMapping();
void Frontend_DispatchKeyEvent(uint32_t scancode, uint32_t keycode,
                               uint32_t mod, bool bDown);
LinAppleKey frontend_to_core_key(int key, uint32_t mod);

// Constants
constexpr int window_width = 560;
constexpr int window_height = 384;
