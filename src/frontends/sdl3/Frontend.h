#pragma once

#include <cstdint>

#include "core/LinAppleCore.h"

#include "frontends/common/AppConfig.h"

// Lifecycle
auto SysInit() -> int;
void SysShutdown();
auto SessionInit(AppConfig* config) -> int;
void SessionShutdown();

void ContinueExecution(uint32_t dwCycles);
void CpuTestHeadless(const char* szTestFile);

// Entry point helpers (implemented in Frontend)
void EnterMessageLoop();
void Sys_Input();
void Sys_Think();
void Sys_Draw();

// Public Frontend helpers (Keyboard translation, etc)
auto DSInit() -> bool;
void DSShutdown();
void SingleStep(bool bReinit);
void Linapple_KeyboardThink(uint32_t dwCycles);
void Frontend_UpdateKeyboardMapping();
void Frontend_DispatchKeyEvent(uint32_t scancode, uint32_t keycode, uint32_t mod, bool bDown);
LinAppleKey Frontend_ToCoreKey(int key, uint32_t mod);

// Constants
constexpr int WINDOW_WIDTH = 560;
constexpr int WINDOW_HEIGHT = 384;
