#pragma once

#include <cstdint>
#include "core/LinAppleCore.h"

// Tier 3: Core Application Logic (Applewin.cpp)
auto SysInit(bool bLog) -> int;
void SysShutdown();
auto SessionInit(const char* szConfigurationFile, bool bSetFullScreen,
                 const char* szImageName_drive1, const char* szImageName_drive2,
                 const char* szSnapshotFile, bool bBoot, bool bPAL) -> int;
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

// Constants
constexpr int WINDOW_WIDTH = 560;
constexpr int WINDOW_HEIGHT = 384;
