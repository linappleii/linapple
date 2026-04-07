#pragma once

#include "Debugger_Types.h"

Update_t CmdGo(int nArgs, const bool bFullSpeed);
Update_t CmdGoNormalSpeed(int nArgs);
Update_t CmdGoFullSpeed(int nArgs);
Update_t CmdStepOver(int nArgs);
Update_t CmdStepOut(int nArgs);
Update_t CmdIn(int nArgs);
Update_t CmdOut(int nArgs);
Update_t CmdRegisterSet(int nArgs);
Update_t CmdJsr(int nArgs);

void CpuSetupBenchmark();

void OutputTraceLine();
void DebugContinueStepping(const bool bCallerWillUpdateDisplay);
void DebugStopStepping(void);
