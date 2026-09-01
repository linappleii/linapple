// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <string>

#include "Debugger_Types.h"

extern SymbolTable_t g_symbols[NUM_SYMBOL_TABLES];

auto _CmdSymbolsClear(SymbolTable_Index_e eSymbolTable) -> Update_t;
auto _CmdSymbolsCommon(int nArgs, SymbolTable_Index_e eSymbolTable) -> Update_t;
auto _CmdSymbolsListTables(int nArgs, int bSymbolTables) -> Update_t;
auto _CmdSymbolsUpdate(int nArgs, int bSymbolTables) -> Update_t;

auto _CmdSymbolList_Address2Symbol(int address, int bSymbolTables) -> bool;
auto _CmdSymbolList_Symbol2Address(const char* pSymbol, int bSymbolTables)
    -> bool;

auto ParseSymbolTable(const std::string& pFileName,
                      SymbolTable_Index_e eWhichTableToLoad,
                      int nSymbolOffset = 0) -> int;
