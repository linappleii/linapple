// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

// Globals
extern MemoryDump mem_dump[NUM_MEM_DUMPS];
extern MemorySearchResults memory_search_results;

// Memory Functions
auto MemoryDumpCheck(int nArgs, uint16_t* pAddress_) -> bool;
auto CmdMemoryCompare(int nArgs) -> UpdateResult;
auto MemoryCheckMiniDump(int iWhich) -> bool;
auto CmdMemoryMiniDumpHex(int nArgs) -> UpdateResult;
auto CmdMemoryMiniDumpAscii(int nArgs) -> UpdateResult;
auto CmdMemoryMiniDumpBin(int nArgs) -> UpdateResult;
auto CmdMemoryDump(int nArgs) -> UpdateResult;
auto CmdMemoryDumpHex(int nArgs) -> UpdateResult;
auto CmdMemoryDumpAscii(int nArgs) -> UpdateResult;
auto CmdMemoryDumpBin(int nArgs) -> UpdateResult;
auto CmdMemoryDumpApple(int nArgs) -> UpdateResult;
auto CmdMemoryDumpByte(int nArgs) -> UpdateResult;
auto CmdMemoryDumpWord(int nArgs) -> UpdateResult;
auto CmdMemoryFill(int nArgs) -> UpdateResult;
auto CmdMemoryMove(int nArgs) -> UpdateResult;
auto CmdMemorySearch(int nArgs) -> UpdateResult;
auto CmdMemorySearchAscii(int nArgs) -> UpdateResult;
auto CmdMemorySearchApple(int nArgs) -> UpdateResult;
auto CmdMemorySearchHex(int nArgs) -> UpdateResult;
auto CmdMemorySearchNext(int nArgs) -> UpdateResult;
auto CmdMemorySet(int nArgs) -> UpdateResult;
auto CmdMemoryVerify(int nArgs) -> UpdateResult;
