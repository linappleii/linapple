// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdGo(int nArgs, bool bFullSpeed) -> UpdateResult;
auto CmdGoNormalSpeed(int nArgs) -> UpdateResult;
auto CmdGoFullSpeed(int nArgs) -> UpdateResult;
auto CmdStepOver(int nArgs) -> UpdateResult;
auto CmdStepOut(int nArgs) -> UpdateResult;
auto CmdIn(int nArgs) -> UpdateResult;
auto CmdOut(int nArgs) -> UpdateResult;
auto CmdRegisterSet(int nArgs) -> UpdateResult;
auto CmdJsr(int nArgs) -> UpdateResult;

auto OutputTraceLine() -> void;
auto DebugContinueStepping(bool bCallerWillUpdateDisplay) -> void;
auto DebugStopStepping(void) -> void;
