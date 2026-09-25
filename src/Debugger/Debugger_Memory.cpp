// SPDX-License-Identifier: GPL-2.0-only
#include "Debugger_Memory.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Help.h"
#include "Debugger_Parser.h"
#include "Debugger_Range.h"
#include "Debugger_Types.h"
#include "apple2/Apple2Types.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

// Globals
MemoryDump_t g_mem_dump[NUM_MEM_DUMPS] = {{true, 0, DEV_MEMORY, MEM_VIEW_HEX},
                                          {false, 0, DEV_MEMORY, MEM_VIEW_HEX}};

// Made global so operator @# can be used with other commands.
MemorySearchResults_t g_memory_search_results;

extern const Opcodes_t* g_opcodes;
extern const Opcodes_t g_opcodes65_c02[NUM_OPCODES];
extern uint16_t g_break_memory_address;

auto debugger_get_file_size(FILE* hFile) -> size_t;
auto CmdWindowViewCommon(int iNewWindow) -> Update_t;

// Internal helpers
static auto CmdMemoryDump(int nArgs, int iWhich, int iView) -> Update_t;
static auto CmdMemorySearch(int nArgs, bool bCaseInsensitive) -> Update_t;

// Memory Functions
// Memory
// _________________________________________________________________________________________

// TO DO:
// . Add support for dumping Disk][ device
//===========================================================================
auto MemoryDumpCheck(int nArgs, uint16_t* pAddress_) -> bool {
  if (!nArgs) {
    return false;
  }

  Arg_t* pArg = &g_args[1];
  uint16_t address = pArg->nValue;
  bool bUpdate = false;

  pArg->eDevice = DEV_MEMORY;  // Default

  if (strncmp(g_args[1].sArg, "SY", 2) == 0)  // Sy6522_t
  {
    address = (g_args[1].sArg[2] - '0') & 3;
    pArg->eDevice = DEV_SY6522;
    bUpdate = true;
  } else if (strncmp(g_args[1].sArg, "AY", 2) == 0)  // Ay8910_t
  {
    address = (g_args[1].sArg[2] - '0') & 3;
    pArg->eDevice = DEV_AY8910;
    bUpdate = true;
  }

  if (bUpdate) {
    pArg->nValue = address;
    snprintf(pArg->sArg, sizeof(pArg->sArg), "%04X", address);
  }

  if (pAddress_) {
    *pAddress_ = address;
  }

  return true;
}

//===========================================================================
auto CmdMemoryCompare(int nArgs) -> Update_t {
  if (nArgs < 3) {
    return Help_Arg_1(CMD_MEMORY_COMPARE);
  }

  uint16_t nSrcAddr = g_args[1].nValue;
  uint16_t nDstAddr = g_args[3].nValue;

  uint16_t nSrcSymAddr = 0;
  uint16_t nDstSymAddr = 0;

  if (!nSrcAddr) {
    nSrcSymAddr = GetAddressFromSymbol(g_args[1].sArg);
    if (nSrcAddr != nSrcSymAddr) {
      nSrcAddr = nSrcSymAddr;
    }
  }

  if (!nDstAddr) {
    nDstSymAddr = GetAddressFromSymbol(g_args[3].sArg);
    if (nDstAddr != nDstSymAddr) {
      nDstAddr = nDstSymAddr;
    }
  }

  //  if ((!nSrcAddr) || (!nDstAddr))
  //    return Help_Arg_1( CMD_MEMORY_COMPARE );

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
static auto CmdMemoryDump(int nArgs, int iWhich, int iView) -> Update_t {
  uint16_t address = 0;

  if (!MemoryDumpCheck(nArgs, &address)) {
    return Help_Arg_1(g_command);
  }

  g_mem_dump[iWhich].address = address;
  g_mem_dump[iWhich].eDevice = g_args[1].eDevice;
  g_mem_dump[iWhich].bActive = true;
  g_mem_dump[iWhich].eView = static_cast<MemoryView_e>(iView);

  if (iWhich == 0) {
    g_disasm_cur_address = address;
  }

  // make sure data window is visible
  if (g_window_this != WINDOW_DATA) {
    CmdWindowViewCommon(WINDOW_DATA);
  }

  return UPDATE_MEM_DUMP;  // TODO: This really needed? Don't think we do any
                           // actual ouput
}

//===========================================================================
auto MemoryCheckMiniDump(int iWhich) -> bool {
  if ((iWhich < 0) || (iWhich > NUM_MEM_MINI_DUMPS)) {
    char sText[CONSOLE_WIDTH];
    snprintf(sText, sizeof(sText), "  Only %d memory mini dumps",
             NUM_MEM_MINI_DUMPS);
    console_display_error(sText);
    return true;
  }
  return false;
}

//===========================================================================
auto CmdMemoryMiniDumpHex(int nArgs) -> Update_t {
  int iWhich = g_command - CMD_MEM_MINI_DUMP_HEX_1;
  if (MemoryCheckMiniDump(iWhich)) {
    return UPDATE_CONSOLE_DISPLAY;
  }

  return CmdMemoryDump(nArgs, iWhich, MEM_VIEW_HEX);
}

//===========================================================================
auto CmdMemoryMiniDumpAscii(int nArgs) -> Update_t {
  int iWhich = g_command - CMD_MEM_MINI_DUMP_ASCII_1;
  if (MemoryCheckMiniDump(iWhich)) {
    return UPDATE_CONSOLE_DISPLAY;
  }

  return CmdMemoryDump(nArgs, iWhich, MEM_VIEW_ASCII);
}

//===========================================================================
auto CmdMemoryMiniDumpApple(int nArgs) -> Update_t {
  int iWhich = g_command - CMD_MEM_MINI_DUMP_APPLE_1;
  if (MemoryCheckMiniDump(iWhich)) {
    return UPDATE_CONSOLE_DISPLAY;
  }

  return CmdMemoryDump(nArgs, iWhich, MEM_VIEW_APPLE);  // MEM_VIEW_TXT_LO );
}

//===========================================================================
// Update_t CmdMemoryMiniDumpLow (int nArgs)
//{
//  int iWhich = g_command - CMD_MEM_MINI_DUMP_TXT_LO_1;
//  if (MemoryCheckMiniDump( iWhich ))
//    return UPDATE_CONSOLE_DISPLAY;
//
//  return CmdMemoryDump(nArgs, iWhich, MEM_VIEW_APPLE ); // MEM_VIEW_TXT_LO );
//}

//===========================================================================
// Update_t CmdMemoryMiniDumpHigh (int nArgs)
//{
//  int iWhich = g_command - CMD_MEM_MINI_DUMP_TXT_HI_1;
//  if (MemoryCheckMiniDump( iWhich ))
//    return UPDATE_CONSOLE_DISPLAY;
//
//  return CmdMemoryDump(nArgs, iWhich, MEM_VIEW_APPLE ); // MEM_VIEW_TXT_HI );
//}

//===========================================================================
auto CmdMemoryEdit(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

// MEB addr 8_bit_value
//===========================================================================
auto CmdMemoryEnterByte(int nArgs) -> Update_t {
  if ((nArgs < 2) ||
      ((g_args[2].sArg[0] != '0') &&
       (!g_args[2].nValue)))  // arg2 not numeric or not specified
  {
    Help_Arg_1(CMD_MEMORY_ENTER_WORD);
  }

  uint16_t address = g_args[1].nValue;
  while (nArgs >= 2) {
    uint16_t nData = g_args[nArgs].nValue;
    if (nData > 0xFF) {
      *(mem + address + nArgs - 2) = static_cast<uint8_t>(nData >> 0);
      *(mem + address + nArgs - 1) = static_cast<uint8_t>(nData >> 8);
    } else {
      *(mem + address + nArgs - 2) = static_cast<uint8_t>(nData);
    }
    *(memdirty + (address >> 8)) = 1;
    nArgs--;
  }

  return UPDATE_ALL;
}

// MEW addr 16-bit_vaue
//===========================================================================
auto CmdMemoryEnterWord(int nArgs) -> Update_t {
  if ((nArgs < 2) ||
      ((g_args[2].sArg[0] != '0') &&
       (!g_args[2].nValue)))  // arg2 not numeric or not specified
  {
    Help_Arg_1(CMD_MEMORY_ENTER_WORD);
  }

  uint16_t address = g_args[1].nValue;
  while (nArgs >= 2) {
    uint16_t nData = g_args[nArgs].nValue;

    // Little Endian
    *(mem + address + nArgs - 2) = static_cast<uint8_t>(nData >> 0);
    *(mem + address + nArgs - 1) = static_cast<uint8_t>(nData >> 8);

    *(memdirty + (address >> 8)) |= 1;
    nArgs--;
  }

  return UPDATE_ALL;
}

//===========================================================================
auto MemMarkDirty(uint16_t nAddressStart, uint16_t nAddressEnd) -> void {
  for (int iPage = (nAddressStart >> 8); iPage <= (nAddressEnd >> 8); iPage++) {
    *(memdirty + iPage) = 1;
  }
}

//===========================================================================
auto CmdMemoryFill(int nArgs) -> Update_t {
  // F address end value
  // F address,len value
  // F address:end value
  if ((!nArgs) || (nArgs < 3) || (nArgs > 4)) {
    return Help_Arg_1(CMD_MEMORY_FILL);
  }

  uint16_t nAddress2 = 0;
  uint16_t nAddressStart = 0;
  uint16_t nAddressEnd = 0;
  int nAddressLen = 0;
  uint8_t nValue = 0;

  if (nArgs == 3) {
    nAddressStart = g_args[1].nValue;
    nAddressEnd = g_args[2].nValue;
    nAddressLen = std::min(static_cast<int>(APPLE2_6502_MEM_END),
                           nAddressEnd - nAddressStart + 1);
  } else {
    RangeType_t eRange;
    eRange = Range_Get(nAddressStart, nAddress2, 1);

    RangeEndLen_t tEndLen = {nAddressEnd, nAddressLen};
    if (!Range_CalcEndLen(eRange, nAddressStart, nAddress2, tEndLen)) {
      nAddressEnd = tEndLen.nAddressEnd;
      nAddressLen = tEndLen.nAddressLen;
      return Help_Arg_1(CMD_MEMORY_MOVE);
    }
  }
#if DEBUG_VAL_2
  nBytes = std::max(1, g_args[1].nVal2);  // TODO: This actually work??
#endif

  if ((nAddressLen > 0) && (nAddressEnd <= APPLE2_6502_MEM_END)) {
    MemMarkDirty(nAddressStart, nAddressEnd);

    nValue = g_args[nArgs].nValue & 0xFF;
    while (nAddressLen--)  // v2.7.0.22
    {
      // TODO: Optimize - split into pre_io, and post_io
      if ((nAddress2 < DBG_6502_IO_BEGIN) || (nAddress2 > DBG_6502_IO_END)) {
        *(mem + nAddressStart) = nValue;
      }
      nAddressStart++;
    }
  }

  return UPDATE_ALL;  // UPDATE_CONSOLE_DISPLAY;
}

static std::string g_memory_load_save_file_name;

// "PWD"
//===========================================================================
auto CmdConfigGetDebugDir(int nArgs) -> Update_t {
  if (nArgs != 0) {
    return Help_Arg_1(CMD_CONFIG_GET_DEBUG_DIR);
  }

  char sPath[path_max_len + 8];
  // TODO: debugger dir has no ` CONSOLE_COLOR_ESCAPE_CHAR ?!?!
  ConsoleBufferPushFormat(sPath, "Path: %s", g_state.current_dir.data());

  return ConsoleUpdate();
}

// "CD"
//===========================================================================
auto CmdConfigSetDebugDir(int nArgs) -> Update_t {
  if (nArgs > 1) {
    return Help_Arg_1(CMD_CONFIG_SET_DEBUG_DIR);
  }

  if (nArgs == 0) {
    return CmdConfigGetDebugDir(0);
  }

  // TODO chdir() not implemented (maybe not needed for Linux?)

  return CmdConfigGetDebugDir(0);  // Show the new PWD
}

//===========================================================================
auto CmdMemoryLoad(int nArgs) -> Update_t {
  // Active memory:
  // BLOAD ["Filename"] , addr[, len]
  // BLOAD ["Filename"] , addr[: end]
  //       1            2 3    4 5
  // Physical 64K memory bank:
  // BLOAD ["Filename"] , bank : addr [, len]
  // BLOAD ["Filename"] , bank : addr [: end]
  //       1            2 3    4 5     6 7
  if (nArgs > 7) {
    return Help_Arg_1(CMD_MEMORY_LOAD);
  }

  if (nArgs < 1) {
    return Help_Arg_1(CMD_MEMORY_LOAD);
  }

  bool bHaveFileName = false;

  if (g_args[1].bType & TYPE_QUOTED_2) {
    bHaveFileName = true;
  }

  //  if (g_args[2].bType & TOKEN_QUOTE_DOUBLE)
  //    bHaveFileName = true;

  int iArgComma1 = 2;
  int iArgAddress = 3;
  int iArgComma2 = 4;
  int iArgLength = 5;
  (void)iArgComma2;
  (void)iArgLength;
  int iArgBank = 3;
  int iArgColon = 4;

  int bank = 0;
  bool bBankSpecified = false;

  if (!bHaveFileName) {
    iArgComma1 = 1;
    iArgAddress = 2;
    iArgComma2 = 3;
    iArgLength = 4;
    iArgBank = 2;
    iArgColon = 3;

    if (nArgs > 6) {
      return Help_Arg_1(CMD_MEMORY_LOAD);
    }
  }

  if (nArgs >= 5) {
    if (!(g_args[iArgBank].bType & TYPE_ADDRESS) ||
        g_args[iArgColon].eToken != TOKEN_COLON) {
      return Help_Arg_1(CMD_MEMORY_LOAD);
    }

    bank = g_args[iArgBank].nValue;
    bBankSpecified = true;

    iArgAddress += 2;
    iArgComma2 += 2;
    iArgLength += 2;
  } else {
    bBankSpecified = false;
  }

  struct KnownFileType_t {
    const char* pExtension;
    int address;
    int nLength;
  };

  const KnownFileType_t aFileTypes[] = {
      {"", 0, 0}  // n/a
      ,
      {".hgr", 0x2000, 0x2000},
      {".hgr2", 0x4000, 0x2000}  // TODO: extension ".dhgr", ".dhgr2"
  };
  const int nFileTypes = sizeof(aFileTypes) / sizeof(KnownFileType_t);
  const KnownFileType_t* pFileType = nullptr;

  char* pFileName = g_args[1].sArg;
  int nLen = strlen(pFileName);
  char* pEnd = pFileName + nLen - 1;
  while (pEnd > pFileName) {
    if (*pEnd == '.') {
      for (int i = 1; i < nFileTypes; i++) {
        if (strcmp(pEnd, aFileTypes[i].pExtension) == 0) {
          pFileType = &aFileTypes[i];
          break;
        }
      }
    }

    if (pFileType) {
      break;
    }

    pEnd--;
  }

  if (!pFileType) {
    if (g_args[iArgComma1].eToken != TOKEN_COMMA) {
      return Help_Arg_1(CMD_MEMORY_LOAD);
    }
  }

  uint16_t nAddressStart = 0;
  uint16_t nAddress2 = 0;
  uint16_t nAddressEnd = 0;
  int nAddressLen = 0;

  if (pFileType) {
    nAddressStart = pFileType->address;
    nAddressLen = pFileType->nLength;
    nAddressEnd = pFileType->nLength + nAddressLen;
  }

  RangeType_t eRange = RANGE_MISSING_ARG_2;

  if (g_args[iArgComma1].eToken == TOKEN_COMMA) {
    eRange = Range_Get(nAddressStart, nAddress2, iArgAddress);
  }

  if (nArgs > iArgComma2) {
    if (eRange == RANGE_MISSING_ARG_2) {
      return Help_Arg_1(CMD_MEMORY_LOAD);
    }

    //    if (eRange == RANGE_MISSING_ARG_2)
    RangeEndLen_t tEndLen = {nAddressEnd, nAddressLen};
    if (!Range_CalcEndLen(eRange, nAddressStart, nAddress2, tEndLen)) {
      nAddressEnd = tEndLen.nAddressEnd;
      nAddressLen = tEndLen.nAddressLen;
      return Help_Arg_1(CMD_MEMORY_LOAD);
    }
  }

  if (bHaveFileName) {
    g_memory_load_save_file_name = pFileName;
  }
  const std::string sLoadSaveFilePath =
      std::string(g_state.current_dir.data()) +
      g_memory_load_save_file_name;  // TODO: g_debug_dir

  uint8_t* const pMemBankBase = bBankSpecified ? mem_get_bank_ptr(bank) : mem;
  if (!pMemBankBase) {
    ConsoleBufferPush("error: Bank out of range.");
    return ConsoleUpdate();
  }

  FilePtr_t hFile(fopen(sLoadSaveFilePath.c_str(), "rb"), fclose);
  if (hFile) {
    size_t nFileBytes = debugger_get_file_size(hFile.get());

    if (nFileBytes > APPLE2_6502_MEM_END) {
      nFileBytes =
          APPLE2_6502_MEM_END + 1;  // Bank-switched RAM/ROM is only 16-bit
    }

    // Caller didn't specify how many bytes to read, default to them all
    if (nAddressLen == 0) {
      nAddressLen = static_cast<int>(nFileBytes);
    }

    size_t nRead = fread(pMemBankBase + nAddressStart,
                         static_cast<size_t>(nAddressLen), 1, hFile.get());
    if (nRead == 1) {
      char text[128];
      ConsoleBufferPushFormat(text, "Loaded @ A$%04X,L$%04X", nAddressStart,
                              nAddressLen);
    } else {
      ConsoleBufferPush("error loading data.");
    }

    if (bBankSpecified) {
      mem_update_paging(true, false);
    } else {
      for (uint16_t i = (nAddressStart >> 8);
           i != ((nAddressStart + static_cast<uint16_t>(nAddressLen)) >> 8);
           i++) {
        memdirty[i] = 0xff;
      }
    }
  } else {
    ConsoleBufferPush("ERROR: Bad filename");

    CmdConfigGetDebugDir(0);

    char sFile[path_max_len + 8];
    ConsoleBufferPushFormat(sFile,
                            "File: ", g_memory_load_save_file_name.c_str());
  }

  return ConsoleUpdate();
}

// dst src : len
//===========================================================================
auto CmdMemoryMove(int nArgs) -> Update_t {
  // M destaddr address end
  // M destaddr address,len
  // M destaddr address:end
  // 2000<4000.5FFFM
  if (nArgs < 3) {
    return Help_Arg_1(CMD_MEMORY_MOVE);
  }

  uint16_t nDst = g_args[1].nValue;
  //  uint16_t nSrc = g_args[2].nValue;
  //  uint16_t nLen = g_args[3].nValue - nSrc;
  uint16_t nAddress2 = 0;
  uint16_t nAddressStart = 0;
  uint16_t nAddressEnd = 0;
  int nAddressLen = 0;

  RangeType_t eRange;
  eRange = Range_Get(nAddressStart, nAddress2, 2);

  //    if (eRange == RANGE_MISSING_ARG_2)
  RangeEndLen_t tEndLen = {nAddressEnd, nAddressLen};
  if (!Range_CalcEndLen(eRange, nAddressStart, nAddress2, tEndLen)) {
    nAddressEnd = tEndLen.nAddressEnd;
    nAddressLen = tEndLen.nAddressLen;
    return Help_Arg_1(CMD_MEMORY_MOVE);
  }

  if ((nAddressLen > 0) && (nAddressEnd <= APPLE2_6502_MEM_END)) {
    MemMarkDirty(nAddressStart, nAddressEnd);

    //      uint8_t *src_ptr = mem + nAddressStart;
    //      uint8_t *pDst = mem + nDst;
    //      uint8_t *pEnd = src_ptr + nAddressLen;

    while (nAddressLen--)  // v2.7.0.23
    {
      // TODO: Optimize - split into pre_io, and post_io
      if ((nDst < DBG_6502_IO_BEGIN) || (nDst > DBG_6502_IO_END)) {
        *(mem + nDst) = *(mem + nAddressStart);
      }
      nDst++;
      nAddressStart++;
    }

    return UPDATE_ALL;
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdMemorySave(int nArgs) -> Update_t {
  // Active memory:
  // BSAVE ["Filename"] , addr , len
  // BSAVE ["Filename"] , addr : end
  //       1            2 3    4 5
  // Physical 64K memory bank:
  // BSAVE ["Filename"] , bank : addr , len
  // BSAVE ["Filename"] , bank : addr : end
  //       1            2 3    4 5    6 7
  static uint16_t nAddressStart = 0;
  uint16_t nAddress2 = 0;
  static uint16_t nAddressEnd = 0;
  static int nAddressLen = 0;
  static int bank = 0;
  static bool bBankSpecified = false;

  if (nArgs > 7) {
    return Help_Arg_1(CMD_MEMORY_SAVE);
  }

  if (!nArgs) {
    char sLast[CONSOLE_WIDTH] = "";
    if (nAddressLen) {
      if (!bBankSpecified) {
        ConsoleBufferPushFormat(sLast, "Last saved: $%04X:$%04X, %04X",
                                nAddressStart, nAddressEnd, nAddressLen);
      } else {
        ConsoleBufferPushFormat(sLast,
                                "Last saved: Bank=%02X $%04X:$%04X, %04X", bank,
                                nAddressStart, nAddressEnd, nAddressLen);
      }
    } else {
      ConsoleBufferPush("Last saved: none");
    }
  } else {
    bool bHaveFileName = false;

    if (g_args[1].bType & TYPE_QUOTED_2) {
      bHaveFileName = true;
    }

    //    int iArgComma1  = 2;
    int iArgAddress = 3;
    int iArgComma2 = 4;
    int iArgLength = 5;
    (void)iArgComma2;
    (void)iArgLength;
    int iArgBank = 3;
    int iArgColon = 4;

    if (!bHaveFileName) {
      //      iArgComma1  = 1;
      iArgAddress = 2;
      iArgComma2 = 3;
      iArgLength = 4;
      iArgBank = 2;
      iArgColon = 3;

      if (nArgs > 6) {
        return Help_Arg_1(CMD_MEMORY_SAVE);
      }
    }

    if (nArgs > 5) {
      if (!(g_args[iArgBank].bType & TYPE_ADDRESS) ||
          g_args[iArgColon].eToken != TOKEN_COLON) {
        return Help_Arg_1(CMD_MEMORY_SAVE);
      }

      bank = g_args[iArgBank].nValue;
      bBankSpecified = true;

      iArgAddress += 2;
      iArgComma2 += 2;
      iArgLength += 2;
    } else {
      bBankSpecified = false;
    }

    //    if ((g_args[ iArgComma1 ].eToken != TOKEN_COMMA) ||
    //      (g_args[ iArgComma2 ].eToken != TOKEN_COLON))
    //      return Help_Arg_1( CMD_MEMORY_SAVE );

    std::string sLoadSaveFilePath =
        g_state.current_dir.data();  // g_state.program_dir

    RangeType_t eRange;
    eRange = Range_Get(nAddressStart, nAddress2, iArgAddress);

    //    if (eRange == RANGE_MISSING_ARG_2)
    RangeEndLen_t tEndLen = {nAddressEnd, nAddressLen};
    if (!Range_CalcEndLen(eRange, nAddressStart, nAddress2, tEndLen)) {
      nAddressEnd = tEndLen.nAddressEnd;
      nAddressLen = tEndLen.nAddressLen;
      return Help_Arg_1(CMD_MEMORY_SAVE);
    }

    if ((nAddressLen) && (nAddressEnd <= APPLE2_6502_MEM_END)) {
      if (!bHaveFileName) {
        char sMemoryLoadSaveFileName[path_max_len];
        if (!bBankSpecified) {
          snprintf(sMemoryLoadSaveFileName, sizeof(sMemoryLoadSaveFileName),
                   "%04X.%04X.bin", nAddressStart, nAddressLen);
        } else {
          snprintf(sMemoryLoadSaveFileName, sizeof(sMemoryLoadSaveFileName),
                   "%04X.%04X.bank%02X.bin", nAddressStart, nAddressLen, bank);
        }
        g_memory_load_save_file_name = sMemoryLoadSaveFileName;
      } else {
        g_memory_load_save_file_name = g_args[1].sArg;
      }
      sLoadSaveFilePath += g_memory_load_save_file_name;

      const uint8_t* const pMemBankBase =
          bBankSpecified ? mem_get_bank_ptr(bank) : mem;
      if (!pMemBankBase) {
        ConsoleBufferPush("error: Bank out of range.");
        return ConsoleUpdate();
      }

      FilePtr_t hFile(fopen(sLoadSaveFilePath.c_str(), "rb"), fclose);
      if (hFile) {
        ConsoleBufferPush("warning: File already exists.  Overwriting.");
        hFile.reset();
        // TODO: BUG: Is this a bug/feature that we can over-write files and the
        // user has no control over that?
      }

      hFile.reset(fopen(sLoadSaveFilePath.c_str(), "wb"));
      if (hFile) {
        size_t nWrote =
            fwrite(pMemBankBase + nAddressStart,
                   static_cast<size_t>(nAddressLen), 1, hFile.get());
        if (nWrote == 1) {
          ConsoleBufferPush("Saved.");
        } else {
          ConsoleBufferPush("error saving.");
        }
      } else {
        ConsoleBufferPush("error opening file.");
      }
    }
  }

  return ConsoleUpdate();
}

char g_text_screen[DEBUG_VIRTUAL_TEXT_HEIGHT *
                   (DEBUG_VIRTUAL_TEXT_WIDTH +
                    4)];  // (80 column + CR + LF) * 24 rows + NUL
int g_text_screen_count = 0;

/*
  $FBC1 BASCALC  IN: A=row, OUT: $28=low, $29=hi
  BASCALC     PHA          ; 000abcde  -> temp
              LSR          ; 0000abcd  CARRY=e y /= 2
              AND #3       ; 000000cd  y & 3
              ORA #4       ; 000001cd  y | 4
              STA $29
              PLA          ; 000abcde <- temp
              AND #$18     ; 000ab000
              BCC BASCLC2  ; e=0?
              ADC #7F      ; 100ab000  yes CARRY=e=1 -> #$+80
  BASCLC2     STA $28      ; e00ab000  no  CARRY=e=0
              ASL          ; 00ab0000
              ASL          ; 0ab00000
              ORA $28      ; 0abab000
              STA $28      ; eabab000

300:A9 00 20 C1 FB A5 29 A6 28 4C 41 F9

y  Hex  000a_bcde            01cd_eaba_b000
 0  00  0000_0000  ->  $400  0100 0000_0000
 1  01  0000_0001  ->  $480  0100 1000_0000
 2  02  0000_0010  ->  $500  0101 0000_0000
 3  03  0000_0011  ->  $580  0101 1000_0000
 4  04  0000_0100  ->  $600  0110 0000_0000
 5  05  0000_0101  ->  $680  0110 1000_0000
 6  06  0000_0110  ->  $700  0111 0000_0000
 7  07  0000_0111  ->  $780  0111 1000_0000

 8  08  0000_1000  ->  $428  0100 0010_1000
 9  09  0000_1001  ->  $4A8  0100 1010_1000
10  0A  0000_1010  ->  $528  0101 0010_1000
11  0B  0000_1011  ->  $5A8  0101 1010_1000
12  0C  0000_1100  ->  $628  0110 0010_1000
13  0D  0000_1101  ->  $6A8  0110 1010_1000
14  0E  0000_1110  ->  $728  0111 0010_1000
15  0F  0000_1111  ->  $7A8  0111 1010_1000

16  10  0001_0000  ->  $450  0100 0101_0000
17  11  0001_0001  ->  $4D0  0100 1101_0000
18  12  0001_0010  ->  $550  0101 0101_0000
19  13  0001_0011  ->  $5D0  0101 1101_0000
20  14  0001_0100  ->  $650  0110 0101_0000
21  15  0001_0101  ->  $6D0  0110_1101_0000
22  16  0001_0110  ->  $750  0111_0101_0000
23  17  0001_0111  ->  $7D0  0111 1101 0000
*/

// Convert ctrl characters to displayable
// Note: FormatCharTxtCtrl() and RemapChar()
static auto RemapChar(const char c) -> char {
  if (c < 0x20) {
    return c + '@';  // Remap INVERSE control character to NORMAL
  } else if (c == 0x7F) {
    return ' ';  // Remap checkboard (DEL) to space
  }

  return c;
}

auto Util_GetDebuggerText(char*& pText_) -> size_t {
  char* pBeg = &g_text_screen[0];
  char* pEnd = &g_text_screen[0];

  g_text_screen_count = 0;
  memset(pBeg, 0, sizeof(g_text_screen));

  memset(g_debugger_virtual_text_screen, 0,
         sizeof(g_debugger_virtual_text_screen));
  debug_display();

  for (auto& y : g_debugger_virtual_text_screen) {
    for (int x = 0; x < DEBUG_VIRTUAL_TEXT_WIDTH; x++) {
      char c = y[x];
      if ((c < 0x20) || (c >= 0x7F)) {
        c = ' ';  // convert null to spaces to keep everything non-proptional
      }
      *pEnd++ = c;
    }
    *pEnd++ = 0x0A;  // LF // OSX, Linux
  }

  *pEnd = 0;
  g_text_screen_count = pEnd - pBeg;

  pText_ = pBeg;
  return g_text_screen_count;
}

auto Util_GetTextScreen(char*& pText_) -> size_t {
  uint16_t nAddressStart = 0;

  char* pBeg = &g_text_screen[0];
  char* pEnd = &g_text_screen[0];

  g_text_screen_count = 0;
  memset(pBeg, 0, sizeof(g_text_screen));

  uint32_t uBank2 = video_get_sw_page2() ? 1 : 0;
  uint8_t* g_text_bank1 = mem_get_aux_ptr(0x400 << uBank2);
  uint8_t* g_text_bank0 = mem_get_main_ptr(0x400 << uBank2);

  for (int y = 0; y < 24; y++) {
    // nAddressStart = 0x400 + (y%8)*0x80 + (y/8)*0x28;
    nAddressStart = ((y & 7) << 7) | ((y & 0x18) << 2) |
                    (y & 0x18);  // no 0x400| since using MemGet*Ptr()

    for (int x = 0; x < 40; x++)  // always 40 columns
    {
      char c = 0;  // TODO: FormatCharTxtCtrl() ?

      if (video_get_sw_80col()) {  // AUX
        c = g_text_bank1[nAddressStart] & 0x7F;
        c = RemapChar(c);
        *pEnd++ = c;
      }  // MAIN -- NOTE: intentional indent & outside if() !

      c = g_text_bank0[nAddressStart] & 0x7F;
      c = RemapChar(c);
      *pEnd++ = c;

      nAddressStart++;
    }

    // Newline // http://en.wikipedia.org/wiki/Newline
    *pEnd++ = 0x0A;  // LF // OSX, Linux
  }
  *pEnd = 0;

  g_text_screen_count = pEnd - pBeg;

  pText_ = pBeg;
  return g_text_screen_count;
}

//===========================================================================
auto CmdNTSC(int nArgs) -> Update_t {
  (void)nArgs;
  return ConsoleUpdate();
}

//===========================================================================
auto CmdTextSave(int nArgs) -> int {
  // Save the TEXT1 40-colomn to text file (Default: AppleWin_Text40.txt"
  // TSAVE ["Filename"]
  // TSAVE ["Filename"]
  //       1
  if (nArgs > 1) {
    return Help_Arg_1(CMD_TEXT_SAVE);
  }

  bool bHaveFileName = false;

  if (g_args[1].bType & TYPE_QUOTED_2) {
    bHaveFileName = true;
  }

  char* text = nullptr;
  size_t nSize = Util_GetTextScreen(text);

  std::string sLoadSaveFilePath =
      g_state.current_dir.data();  // g_state.program_dir

  if (bHaveFileName) {
    g_memory_load_save_file_name = g_args[1].sArg;
  } else {
    if (video_get_sw_80col()) {
      g_memory_load_save_file_name = "AppleWin_Text80.txt";
    } else {
      g_memory_load_save_file_name = "AppleWin_Text40.txt";
    }
  }

  sLoadSaveFilePath += g_memory_load_save_file_name;

  FilePtr_t hFile(fopen(sLoadSaveFilePath.c_str(), "rb"), fclose);
  if (hFile) {
    ConsoleBufferPush("warning: File already exists.  Overwriting.");
    hFile.reset();
  }

  hFile.reset(fopen(sLoadSaveFilePath.c_str(), "wb"));
  if (hFile) {
    size_t nWrote = fwrite(text, nSize, 1, hFile.get());
    if (nWrote == 1) {
      char text[CONSOLE_WIDTH] = "";
      ConsoleBufferPushFormat(text, "Saved: %s",
                              g_memory_load_save_file_name.c_str());
    } else {
      ConsoleBufferPush("error saving.");
    }
  } else {
    ConsoleBufferPush("error opening file.");
  }

  return ConsoleUpdate();
}

//===========================================================================
auto SearchMemoryFind(MemorySearchValues_t vMemorySearchValues,
                      uint16_t nAddressStart, uint16_t nAddressEnd) -> int {
  int nFound = 0;
  g_memory_search_results.erase(g_memory_search_results.begin(),
                                g_memory_search_results.end());
  g_memory_search_results.push_back(NO_6502_TARGET);

  uint16_t address = 0;
  for (address = nAddressStart; address < nAddressEnd; address++) {
    bool bMatchAll = true;

    uint16_t nAddress2 = address;

    int nMemBlocks = vMemorySearchValues.size();
    for (int iBlock = 0; iBlock < nMemBlocks; iBlock++, nAddress2++) {
      MemorySearch_t ms = vMemorySearchValues.at(iBlock);
      ms.found = false;

      if ((ms.type == MEM_SEARCH_BYTE_EXACT) ||
          (ms.type == MEM_SEARCH_NIB_HIGH_EXACT) ||
          (ms.type == MEM_SEARCH_NIB_LOW_EXACT)) {
        uint8_t nTarget = *(mem + nAddress2);

        if (ms.type == MEM_SEARCH_NIB_LOW_EXACT) {
          nTarget &= 0x0F;
        }

        if (ms.type == MEM_SEARCH_NIB_HIGH_EXACT) {
          nTarget &= 0xF0;
        }

        if (ms.value == nTarget) {
          ms.found = true;
          continue;
        } else {
          bMatchAll = false;
          break;
        }
      } else if (ms.type == MEM_SEARCH_BYTE_1_WILD) {
        // match by definition
      } else {
        // start 2ndary search
        // if next block matches, then this block matches (since we are wild)
        if ((iBlock + 1) ==
            nMemBlocks) {  // there is no next block, hence we match
          continue;
        }

        //        MemorySearch_t ms2 = vMemorySearchValues.at( iBlock + 1 );

        uint16_t nAddress3 = nAddress2;
        for (nAddress3 = nAddress2; nAddress3 < nAddressEnd; nAddress3++) {
          if ((ms.type == MEM_SEARCH_BYTE_EXACT) ||
              (ms.type == MEM_SEARCH_NIB_HIGH_EXACT) ||
              (ms.type == MEM_SEARCH_NIB_LOW_EXACT)) {
            uint8_t nTarget = *(mem + nAddress3);

            if (ms.type == MEM_SEARCH_NIB_LOW_EXACT) {
              nTarget &= 0x0F;
            }

            if (ms.type == MEM_SEARCH_NIB_HIGH_EXACT) {
              nTarget &= 0xF0;
            }

            if (ms.value == nTarget) {
              nAddress2 = nAddress3;
              continue;
            } else {
              bMatchAll = false;
              break;
            }
          }
        }
      }
    }

    if (bMatchAll) {
      nFound++;

      // Save the search result
      g_memory_search_results.push_back(address);
    }
  }

  return nFound;
}

auto SearchMemoryDisplay(int nArgs) -> Update_t {
  (void)nArgs;
  const uint32_t nBuf = CONSOLE_WIDTH * 2;

  int nFound = g_memory_search_results.size() - 1;

  int nLen = 0;      // temp
  int nLineLen = 0;  // string length of matches for this line, for word-wrap

  char sMatches[nBuf] = "";
  char sResult[nBuf];
  char sText[nBuf] = "";

  if (nFound > 0) {
    int iFound = 1;
    while (iFound <= nFound) {
      uint16_t address = g_memory_search_results.at(iFound);

      //      sprintf( sText, "%2d:$%04X ", iFound, address );
      //      int nLen = strlen( sText );

      sResult[0] = 0;
      nLen = 0;

      StringCat(sResult, CHC_NUM_DEC,
                nBuf);  // 2.6.2.17 Search Results: The n'th result now using
                        // correct color (was command, now number decimal)
      snprintf(sText, sizeof(sText), "%02X",
               iFound);  // BUGFIX: 2.6.2.32 n'th Search results were being
                         // displayed in dec, yet parser takes hex numbers. i.e.
                         // SH D000:FFFF A9 00
      nLen += StringCat(sResult, sText, nBuf);

      StringCat(sResult, CHC_DEFAULT,
                nBuf);  // intentional default instead of CHC_ARG_SEP for better
                        // readability
      nLen += StringCat(sResult, ":", nBuf);

      StringCat(sResult, CHC_ARG_SEP, nBuf);
      nLen +=
          StringCat(sResult, "$",
                    nBuf);  // 2.6.2.16 Fixed: Search Results: The hex specify
                            // for target address results now colorized properly

      StringCat(sResult, CHC_ADDRESS, nBuf);
      snprintf(sText, sizeof(sText), "%04X ",
               address);  // 2.6.2.15 Fixed: Search Results: Added space between
                          // results for better readability
      nLen += StringCat(sResult, sText, nBuf);

      // Fit on same line?
      if ((nLineLen + nLen) > (g_console_display_width - 1))  // CONSOLE_WIDTH
      {
        // ConsoleDisplayPush( sMatches );
        console_print(sMatches);
        util_safe_strcpy(sMatches, sResult, sizeof(sMatches));
        nLineLen = nLen;
      } else {
        StringCat(sMatches, sResult, nBuf);
        nLineLen += nLen;
      }

      iFound++;
    }
    console_print(sMatches);
  }

  //  wsprintf( sMatches, "Total: %d  (#$%04X)", nFound, nFound );
  //  ConsoleDisplayPush( sMatches );
  sResult[0] = 0;

  StringCat(sResult, CHC_USAGE, nBuf);
  nLen += StringCat(sResult, "Total", nBuf);

  StringCat(sResult, CHC_DEFAULT, nBuf);
  nLen += StringCat(sResult, ": ", nBuf);

  StringCat(sResult, CHC_NUM_DEC, nBuf);  // intentional CHC_DEFAULT instead of
  snprintf(sText, sizeof(sText), "%d  ", nFound);
  nLen += StringCat(sResult, sText, nBuf);

  StringCat(sResult, CHC_ARG_SEP, nBuf);  // CHC_ARC_OPT -> CHC_ARG_SEP
  nLen += StringCat(sResult, "(", nBuf);

  StringCat(sResult, CHC_ARG_SEP, nBuf);  // CHC_DEFAULT
  nLen += StringCat(sResult, "#$", nBuf);

  StringCat(sResult, CHC_NUM_HEX, nBuf);
  snprintf(sText, sizeof(sText), "%04X", nFound);
  nLen += StringCat(sResult, sText, nBuf);

  StringCat(sResult, CHC_ARG_SEP, nBuf);
  nLen += StringCat(sResult, ")", nBuf);

  console_print(sResult);

  // g_memory_search_results is cleared in debug_end()

  //  return UPDATE_CONSOLE_DISPLAY;
  return ConsoleUpdate();
}

//===========================================================================
auto CmdMemorySearch(int nArgs, bool bTextIsAscii = true) -> Update_t {
  (void)bTextIsAscii;
  uint16_t nAddressStart = 0;
  uint16_t nAddress2 = 0;
  uint16_t nAddressEnd = 0;
  int nAddressLen = 0;

  RangeType_t eRange;
  eRange = Range_Get(nAddressStart, nAddress2);

  //  if (eRange == RANGE_MISSING_ARG_2)
  RangeEndLen_t tEndLen = {nAddressEnd, nAddressLen};
  if (!Range_CalcEndLen(eRange, nAddressStart, nAddress2, tEndLen)) {
    nAddressEnd = tEndLen.nAddressEnd;
    nAddressLen = tEndLen.nAddressLen;
    return console_display_error(
        "error: Missing address seperator (comma or colon");
  }

  int iArgFirstByte = 4;
  int iArg = 0;

  MemorySearchValues_t vMemorySearchValues;
  MemorySearch_e tLastType = MEM_SEARCH_BYTE_N_WILD;

  // Get search "string"
  Arg_t* pArg = &g_args[iArgFirstByte];

  uint16_t nTarget = 0;
  for (iArg = iArgFirstByte; iArg <= nArgs; iArg++, pArg++) {
    MemorySearch_t ms{};

    nTarget = pArg->nValue;
    ms.value = nTarget & 0xFF;
    ms.type = MEM_SEARCH_BYTE_EXACT;

    if (nTarget > 0xFF)  // searching for 16-bit address
    {
      vMemorySearchValues.push_back(ms);
      ms.value = (nTarget >> 8);

      tLastType = ms.type;
    } else {
      char* pByte = pArg->sArg;

      if (pArg->bType & TYPE_QUOTED_1) {
        // Convert string to hex byte(s)
        int iChar = 0;
        int nChars = pArg->nArgLen;

        if (nChars) {
          ms.type = MEM_SEARCH_BYTE_EXACT;
          ms.found = false;

          while (iChar < nChars) {
            ms.value = pArg->sArg[iChar];
            ms.value |= 0x80;

            // last char is handle in common case below
            iChar++;
            if (iChar < nChars) {
              vMemorySearchValues.push_back(ms);
            }
          }
        }
      } else if (pArg->bType & TYPE_QUOTED_2) {
        // Convert string to hex byte(s)
        int iChar = 0;
        int nChars = pArg->nArgLen;

        if (nChars) {
          ms.type = MEM_SEARCH_BYTE_EXACT;
          ms.found = false;

          while (iChar < nChars) {
            ms.value = pArg->sArg[iChar];
            ms.value &= 0x7F;

            iChar++;  // last char is handle in common case below
            if (iChar < nChars) {
              vMemorySearchValues.push_back(ms);
            }
          }
        }
      } else {
        // must be numeric .. make sure not too big
        if (pArg->nArgLen > 2) {
          vMemorySearchValues.erase(vMemorySearchValues.begin(),
                                    vMemorySearchValues.end());
          return HelpLastCommand();
        }

        if (pArg->nArgLen == 1) {
          if (pByte[0] == g_parameters[PARAM_MEM_SEARCH_WILD]
                              .name[0])  // Hack: hard-coded one char token
          {
            ms.type = MEM_SEARCH_BYTE_1_WILD;
          }
        } else {
          if (pByte[0] == g_parameters[PARAM_MEM_SEARCH_WILD]
                              .name[0])  // Hack: hard-coded one char token
          {
            ms.type = MEM_SEARCH_NIB_LOW_EXACT;
            ms.value = pArg->nValue & 0x0F;
          }

          if (pByte[1] == g_parameters[PARAM_MEM_SEARCH_WILD]
                              .name[0])  // Hack: hard-coded one char token
          {
            if (ms.type == MEM_SEARCH_NIB_LOW_EXACT) {
              ms.type = MEM_SEARCH_BYTE_N_WILD;
            } else {
              ms.type = MEM_SEARCH_NIB_HIGH_EXACT;
              ms.value = (pArg->nValue << 4) & 0xF0;
            }
          }
        }
      }
    }

    // skip over multiple byte_wild, since they are redundent
    // xx ?? ?? xx
    //       ^
    //       redundant
    if ((tLastType == MEM_SEARCH_BYTE_N_WILD) &&
        (ms.type == MEM_SEARCH_BYTE_N_WILD)) {
      continue;
    }

    vMemorySearchValues.push_back(ms);
    tLastType = ms.type;
  }

  SearchMemoryFind(vMemorySearchValues, nAddressStart, nAddressEnd);
  vMemorySearchValues.erase(vMemorySearchValues.begin(),
                            vMemorySearchValues.end());

  return SearchMemoryDisplay();
}

//===========================================================================
auto CmdMemorySearch(int nArgs) -> Update_t {
  // S address,length # [,#]
  if (nArgs < 4) {
    return HelpLastCommand();
  }

  return CmdMemorySearch(nArgs, true);

  return UPDATE_CONSOLE_DISPLAY;
}

// Search for ASCII text (no Hi-Bit set)
//===========================================================================
auto CmdMemorySearchAscii(int nArgs) -> Update_t {
  if (nArgs < 4) {
    return HelpLastCommand();
  }

  return CmdMemorySearch(nArgs, true);
}

// Search for Apple text (Hi-Bit set)
//===========================================================================
auto CmdMemorySearchApple(int nArgs) -> Update_t {
  if (nArgs < 4) {
    return HelpLastCommand();
  }

  return CmdMemorySearch(nArgs, false);
}

//===========================================================================
auto CmdMemorySearchHex(int nArgs) -> Update_t {
  if (nArgs < 4) {
    return HelpLastCommand();
  }

  return CmdMemorySearch(nArgs, true);
}
