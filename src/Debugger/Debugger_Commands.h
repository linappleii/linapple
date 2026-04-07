#pragma once

#include "Debugger_Types.h"

extern Command_t g_aCommands[];
extern int g_nNumCommandsWithAliases;

void VerifyDebuggerCommandTable();
Update_t DebuggerProcessCommand(const bool bEchoConsoleInput);
Update_t ExecuteCommand(int nArgs);
