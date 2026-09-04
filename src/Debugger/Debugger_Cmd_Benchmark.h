// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdBenchmark(int nArgs) -> Update_t;
auto CmdBenchmarkStart(int nArgs) -> Update_t;
auto CmdBenchmarkStop(int nArgs) -> Update_t;
auto CmdProfile(int nArgs) -> Update_t;

auto ProfileReset() -> void;
auto ProfileSave() -> bool;
auto ProfileFormat(bool bSeperateColumns, int eFormatMode) -> void;
auto ProfileLinePeek(int iLine) -> char*;
auto ProfileLinePush() -> char*;
auto ProfileLineReset() -> void;
