#pragma once

#include "Debugger_Types.h"

Update_t CmdOutputCalc(int nArgs);
Update_t CmdOutputEcho(int nArgs);
Update_t CmdOutputPrint(int nArgs);
Update_t CmdOutputPrintf(int nArgs);
Update_t CmdOutputRun(int nArgs);

void DebuggerRunScript(const char* pFileName);
