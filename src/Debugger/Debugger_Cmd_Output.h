// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdOutputCalc(int nArgs) -> Update_t;
auto CmdOutputEcho(int nArgs) -> Update_t;
auto CmdOutputPrint(int nArgs) -> Update_t;
auto CmdOutputPrintf(int nArgs) -> Update_t;
auto CmdOutputRun(int nArgs) -> Update_t;

auto DebuggerRunScript(const char* pFileName) -> void;
