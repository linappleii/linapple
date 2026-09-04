// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

// Globals
extern MemoryDump_t g_mem_dump[NUM_MEM_DUMPS];
extern MemorySearchResults_t g_memory_search_results;

// Memory Functions
auto MemoryDumpCheck(int nArgs, uint16_t* pAddress_) -> bool;
auto CmdMemoryCompare(int nArgs) -> Update_t;
auto MemoryCheckMiniDump(int iWhich) -> bool;
auto CmdMemoryMiniDumpHex(int nArgs) -> Update_t;
auto CmdMemoryMiniDumpAscii(int nArgs) -> Update_t;
auto CmdMemoryMiniDumpBin(int nArgs) -> Update_t;
auto CmdMemoryDump(int nArgs) -> Update_t;
auto CmdMemoryDumpHex(int nArgs) -> Update_t;
auto CmdMemoryDumpAscii(int nArgs) -> Update_t;
auto CmdMemoryDumpBin(int nArgs) -> Update_t;
auto CmdMemoryDumpApple(int nArgs) -> Update_t;
auto CmdMemoryDumpByte(int nArgs) -> Update_t;
auto CmdMemoryDumpWord(int nArgs) -> Update_t;
auto CmdMemoryFill(int nArgs) -> Update_t;
auto CmdMemoryMove(int nArgs) -> Update_t;
auto CmdMemorySearch(int nArgs) -> Update_t;
auto CmdMemorySearchAscii(int nArgs) -> Update_t;
auto CmdMemorySearchApple(int nArgs) -> Update_t;
auto CmdMemorySearchHex(int nArgs) -> Update_t;
auto CmdMemorySearchNext(int nArgs) -> Update_t;
auto CmdMemorySet(int nArgs) -> Update_t;
auto CmdMemoryVerify(int nArgs) -> Update_t;
