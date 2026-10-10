// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdZeroPage(int nArgs) -> UpdateResult;
auto CmdZeroPageAdd(int nArgs) -> UpdateResult;
auto CmdZeroPageClear(int nArgs) -> UpdateResult;
auto CmdZeroPageDisable(int nArgs) -> UpdateResult;
auto CmdZeroPageEnable(int nArgs) -> UpdateResult;
auto CmdZeroPageList(int nArgs) -> UpdateResult;
auto CmdZeroPageSave(int nArgs) -> UpdateResult;
auto CmdZeroPagePointer(int nArgs) -> UpdateResult;
