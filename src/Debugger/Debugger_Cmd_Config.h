// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdConfigColorMono(int nArgs) -> Update_t;
auto CmdConfigHColor(int nArgs) -> Update_t;
auto CmdConfigLoad(int nArgs) -> Update_t;
auto CmdConfigSave(int nArgs) -> Update_t;
auto CmdConfigDisasm(int nArgs) -> Update_t;
auto CmdConfigFontLoad(int nArgs) -> Update_t;
auto CmdConfigFontSave(int nArgs) -> Update_t;
auto CmdConfigFontMode(int nArgs) -> Update_t;
auto CmdConfigFont(int nArgs) -> Update_t;
auto CmdConfigSetFont(int nArgs) -> Update_t;
auto CmdConfigGetFont(int nArgs) -> Update_t;
auto CmdConfigSetDebugDir(int nArgs) -> Update_t;

auto ConfigSave_BufferToDisk(const char* pFileName, ConfigSave_t eConfigSave)
    -> bool;
auto ConfigSave_PrepareHeader(const Parameters_e eCategory,
                              const Commands_e eCommandClear) -> void;
auto UpdateWindowFontHeights(int nFontHeight) -> void;
