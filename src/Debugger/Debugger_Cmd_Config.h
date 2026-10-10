// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"

extern MemoryTextFile config_state;
extern bool report_missing_scripts;

auto CmdConfigColorMono(int nArgs) -> UpdateResult;
auto CmdConfigHColor(int nArgs) -> UpdateResult;
auto CmdConfigLoad(int nArgs) -> UpdateResult;
auto CmdConfigSave(int nArgs) -> UpdateResult;
auto CmdConfigDisasm(int nArgs) -> UpdateResult;
auto CmdConfigFontLoad(int nArgs) -> UpdateResult;
auto CmdConfigFontSave(int nArgs) -> UpdateResult;
auto CmdConfigFontMode(int nArgs) -> UpdateResult;
auto CmdConfigFont(int nArgs) -> UpdateResult;
auto CmdConfigSetFont(int nArgs) -> UpdateResult;
auto CmdConfigGetFont(int nArgs) -> UpdateResult;
auto CmdConfigSetDebugDir(int nArgs) -> UpdateResult;

auto ConfigSave_BufferToDisk(const char* pFileName, ConfigSave eConfigSave)
    -> bool;
auto ConfigSave_PrepareHeader(Parameters eCategory, Commands eCommandClear)
    -> void;
auto UpdateWindowFontHeights(int nFontHeight) -> void;
