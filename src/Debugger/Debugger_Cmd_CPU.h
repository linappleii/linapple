// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdGo(int nArgs, const bool bFullSpeed) -> Update_t;
auto CmdGoNormalSpeed(int nArgs) -> Update_t;
auto CmdGoFullSpeed(int nArgs) -> Update_t;
auto CmdStepOver(int nArgs) -> Update_t;
auto CmdStepOut(int nArgs) -> Update_t;
auto CmdIn(int nArgs) -> Update_t;
auto CmdOut(int nArgs) -> Update_t;
auto CmdRegisterSet(int nArgs) -> Update_t;
auto CmdJsr(int nArgs) -> Update_t;

auto cpu_setup_benchmark() -> void;

auto OutputTraceLine() -> void;
auto DebugContinueStepping(const bool bCallerWillUpdateDisplay) -> void;
auto DebugStopStepping(void) -> void;
