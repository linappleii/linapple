// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdOutputCalc(int nArgs) -> UpdateResult;
auto CmdOutputEcho(int nArgs) -> UpdateResult;
auto CmdOutputPrint(int nArgs) -> UpdateResult;
auto CmdOutputPrintf(int nArgs) -> UpdateResult;
auto CmdOutputRun(int nArgs) -> UpdateResult;

auto DebuggerRunScript(const char* pFileName) -> void;
