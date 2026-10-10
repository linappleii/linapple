// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

extern Command commands[];
extern int num_commands_with_aliases;

auto VerifyDebuggerCommandTable() -> void;
auto DebuggerProcessCommand(bool bEchoConsoleInput) -> UpdateResult;
auto ExecuteCommand(int nArgs) -> UpdateResult;
