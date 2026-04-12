#pragma once

#include <cstdint>
#include <string>
#include <vector>

using std::string;
using std::vector;

// Tier 3: Core Application Logic (Applewin.cpp)
int SysInit(bool bLog);
void SysShutdown();
int SessionInit(const char* szConfigurationFile, bool bSetFullScreen,
                const char* szImageName_drive1, const char* szImageName_drive2,
                const char* szSnapshotFile, bool bBoot, bool bPAL);
void SessionShutdown();
void ContinueExecution(uint32_t dwCycles);
void CpuTestHeadless(const char* szTestFile);

// Entry point helpers (implemented in Frontend)
void EnterMessageLoop();
void Sys_Input();
void Sys_Think();
void Sys_Draw();

// Public Frontend helpers (Keyboard translation, etc)
bool DSInit();
void DSShutdown();
void SingleStep(bool bReinit);
extern "C" void Linapple_SetAppleKey(int apple_key, bool bDown);
void Linapple_SetCapsLockState(bool bEnabled);
extern "C" void Linapple_SetKeyState(uint8_t apple_code, bool bDown);
void Linapple_KeyboardThink(uint32_t dwCycles);

// Constants
#define WINDOW_WIDTH  560
#define WINDOW_HEIGHT 384
