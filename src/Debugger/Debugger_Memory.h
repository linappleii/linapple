#pragma once

#include "Debugger_Types.h"

// Globals
extern MemoryDump_t g_aMemDump[NUM_MEM_DUMPS];
extern MemorySearchResults_t g_vMemorySearchResults;

// Memory Functions
bool MemoryDumpCheck(int nArgs, unsigned short *pAddress_);
Update_t CmdMemoryCompare(int nArgs);
bool _MemoryCheckMiniDump(int iWhich);
Update_t CmdMemoryMiniDumpHex(int nArgs);
Update_t CmdMemoryMiniDumpAscii(int nArgs);
Update_t CmdMemoryMiniDumpBin(int nArgs);
Update_t CmdMemoryDump(int nArgs);
Update_t CmdMemoryDumpHex(int nArgs);
Update_t CmdMemoryDumpAscii(int nArgs);
Update_t CmdMemoryDumpBin(int nArgs);
Update_t CmdMemoryDumpApple(int nArgs);
Update_t CmdMemoryDumpByte(int nArgs);
Update_t CmdMemoryDumpWord(int nArgs);
Update_t CmdMemoryFill(int nArgs);
Update_t CmdMemoryMove(int nArgs);
Update_t CmdMemorySearch(int nArgs);
Update_t CmdMemorySearchAscii(int nArgs);
Update_t CmdMemorySearchApple(int nArgs);
Update_t CmdMemorySearchHex(int nArgs);
Update_t CmdMemorySearchNext(int nArgs);
Update_t CmdMemorySet(int nArgs);
Update_t CmdMemoryVerify(int nArgs);
