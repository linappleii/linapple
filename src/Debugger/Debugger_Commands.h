// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

extern Command_t g_commands[];
extern int g_num_commands_with_aliases;

void VerifyDebuggerCommandTable();
Update_t DebuggerProcessCommand(const bool bEchoConsoleInput);
Update_t ExecuteCommand(int nArgs);
