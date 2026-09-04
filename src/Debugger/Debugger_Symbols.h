// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <string>

#include "Debugger_Types.h"

extern SymbolTable_t g_symbols[NUM_SYMBOL_TABLES];

auto CmdSymbolsClear(SymbolTable_Index_e eSymbolTable) -> Update_t;
auto CmdSymbolsCommon(int nArgs, SymbolTable_Index_e eSymbolTable) -> Update_t;
auto CmdSymbolsListTables(int nArgs, int bSymbolTables) -> Update_t;
auto CmdSymbolsUpdate(int nArgs, int bSymbolTables) -> Update_t;

auto CmdSymbolList_Address2Symbol(int address, int bSymbolTables) -> bool;
auto CmdSymbolList_Symbol2Address(const char* pSymbol, int bSymbolTables)
    -> bool;

auto ParseSymbolTable(const std::string& pFileName,
                      SymbolTable_Index_e eWhichTableToLoad,
                      int nSymbolOffset = 0) -> int;
