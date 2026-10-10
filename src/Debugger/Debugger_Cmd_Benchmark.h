// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdBenchmark(int nArgs) -> UpdateResult;
auto CmdBenchmarkStart(int nArgs) -> UpdateResult;
auto CmdBenchmarkStop(int nArgs) -> UpdateResult;
auto CmdProfile(int nArgs) -> UpdateResult;

auto ProfileReset() -> void;
auto ProfileSave() -> bool;
auto ProfileFormat(bool bSeperateColumns, int eFormatMode) -> void;
auto ProfileLinePeek(int iLine) -> char*;
auto ProfileLinePush() -> char*;
auto ProfileLineReset() -> void;
