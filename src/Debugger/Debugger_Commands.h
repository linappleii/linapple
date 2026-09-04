// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

extern Command_t g_commands[];
extern int g_num_commands_with_aliases;

auto VerifyDebuggerCommandTable() -> void;
auto DebuggerProcessCommand(const bool bEchoConsoleInput) -> Update_t;
auto ExecuteCommand(int nArgs) -> Update_t;
