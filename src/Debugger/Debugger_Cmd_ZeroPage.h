// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdZeroPage(int nArgs) -> Update_t;
auto CmdZeroPageAdd(int nArgs) -> Update_t;
auto CmdZeroPageClear(int nArgs) -> Update_t;
auto CmdZeroPageDisable(int nArgs) -> Update_t;
auto CmdZeroPageEnable(int nArgs) -> Update_t;
auto CmdZeroPageList(int nArgs) -> Update_t;
auto CmdZeroPageSave(int nArgs) -> Update_t;
auto CmdZeroPagePointer(int nArgs) -> Update_t;
