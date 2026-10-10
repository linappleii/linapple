// SPDX-License-Identifier: GPL-2.0-only

#include "Debugger_DisassemblerData.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Range.h"
#include "Debugger_Types.h"
#include "Util_Text.h"

// Disassembler Data
// ______________________________________________________________________________

// __ Debugger Interface
// ____________________________________________________________________________

//===========================================================================
static auto CmdDefineByteRange(int nArgs, int iArg, DisasmData& tData_)
    -> uint16_t {
  uint16_t address = 0;
  uint16_t nAddress2 = 0;
  int nLen = 0;

  memset(reinterpret_cast<void*>(&tData_), 0, sizeof(tData_));

  // DB address
  // DB symbol
  // bool bisAddress ...

  if (nArgs < 1) {
    address = disasm_cur_address;
  } else {
    RangeType eRange = Range_Get(address, nAddress2, iArg);
    if ((eRange == RANGE_HAS_END) || (eRange == RANGE_HAS_LEN)) {
      RangeEndLen tEndLen{};
      Range_CalcEndLen(eRange, address, nAddress2, tEndLen);
      nLen = tEndLen.nAddressLen;
      nLen--;  // Disassembly_IsDataAddress() is *inclusive* // KEEP IN SYNC:
               // CmdDefineByteRange() CmdDisasmDataList()
               // GetOpmodeOpbyte() FormatNopcodeBytes()
    } else {
      if (nArgs > 1) {
        address = args[2].nValue;
      } else {
        address = args[1].nValue;
      }
    }
  }

  // 2.7.0.35 DW address -- round the length up to even number for convenience.
  // Example: 'DW 6062' is equivalent to: 'DW 6062:6063'
  if ((command == CMD_DEFINE_DATA_WORD1) && ((~nLen & 1) != 0)) {
    nLen++;
  }

  tData_.nStartAddress = address;
  tData_.nEndAddress = address + nLen;
  //	tData_.nArraySize = 0;

  const char* pSymbolName = "";
  char aSymbolName[MAX_SYMBOLS_LEN + 1];
  SymbolTable_Index_e eSymbolTable = SYMBOLS_ASSEMBLY;
  bool bAutoDefineName = false;  // 2.7.0.34

  if (nArgs > 1) {
    if (args[2].eToken ==
        TOKEN_COLON)  // 2.7.0.31 Bug fix: DB range, i.e. DB 174E:174F
    {
      bAutoDefineName = true;
    } else {
      pSymbolName = args[1].sArg;
      util_safe_strcpy(aSymbolName, pSymbolName, sizeof(aSymbolName));
      pSymbolName = aSymbolName;
    }
  } else {
    bAutoDefineName = true;
  }

  // 2.7.0.34 Unified auto-defined name: B_, W_, T_ for byte, word, or text
  // respectively Old name: auto define D_# DB $XX Example 'DB' or 'DW' with 1
  // arg
  //   DB 801
  if (bAutoDefineName) {
    if (command == CMD_DEFINE_DATA_STR) {
      snprintf(aSymbolName, sizeof(aSymbolName), "T_%04X",
               tData_.nStartAddress);  // ASC range
    } else if (command == CMD_DEFINE_DATA_WORD1) {
      snprintf(aSymbolName, sizeof(aSymbolName), "W_%04X",
               tData_.nStartAddress);  // DW range
    } else {
      snprintf(aSymbolName, sizeof(aSymbolName), "B_%04X",
               tData_.nStartAddress);  // DB range
    }

    pSymbolName = aSymbolName;
  }

  // bRemoveSymbol = false // use arg[2]
  // bUpdateSymbol = true // add the symbol to the table
  SymbolUpdate(eSymbolTable, pSymbolName, address, false, true);

  // TODO: Note: need to call ConsoleUpdate(), as may print symbol has been
  // updated

  util_safe_strcpy(tData_.sSymbol, pSymbolName, sizeof(tData_.sSymbol));

  return address;
}

// Undefine Data
//===========================================================================
auto CmdDisasmDataDefCode(int nArgs) -> UpdateResult {
  // treat memory (bytes) as code
  if ((nArgs > 2) && (nArgs != 4)) {
    return Help_Arg_1(CMD_DISASM_CODE);
  }

  DisasmData tData{};
  int iArg = 2;
  uint16_t address = CmdDefineByteRange(nArgs, iArg, tData);

  // Need to iterate through all blocks
  // DB TEST1 300:320
  // DB TEST2 310:330
  // DB TEST3 320:340
  // X  TEST1

  DisasmData* data = Disassembly_IsDataAddress(address);
  if (data) {
    // TODO: Do we need to split the data !?
    // Disassembly_DelData( tData );
    data->iDirective = NOP_REMOVED;

    // TODO: Remove symbol 'D_FA62' from symbol table!
  } else {
    Disassembly_DelData(tData);
  }

  return UPDATE_DISASM | ConsoleUpdate();
}

static const char* const nopcode_types[NUM_NOPCODE_TYPES] = {
    "-n/a-", "byte1", "byte2", "byte4", "byte8", "word1", "word2", "word4",
    "addr ", "hex  ", "char ", "ascii", "apple", "mixed", "FAC  ", "bmp  ",
};

// Command: B
// no args
// List the data blocks
//===========================================================================
auto CmdDisasmDataList(int nArgs) -> UpdateResult {
  (void)nArgs;

  // Need to iterate through all blocks
  DisasmData* data = nullptr;

  while ((data = Disassembly_Enumerate(data))) {
    if (data->iDirective != NOP_REMOVED) {
      int nLen = strlen(data->sSymbol);

      char sText[CONSOLE_WIDTH * 2];
      // <smbol> <type> <start>:<end>
      // `TEST `300`:`320
      ConsolePrintFormat(
          sText, "%s%s %s%*s %s%04X%s:%s%04X", CHC_CATEGORY,
          nopcode_types[data->eElementType],
          (nLen > 0) ? CHC_SYMBOL : CHC_DEFAULT, MAX_SYMBOLS_LEN,
          (nLen > 0) ? data->sSymbol : "???", CHC_ADDRESS, data->nStartAddress,
          CHC_ARG_SEP, CHC_ADDRESS,
          data->nEndAddress  // Disassembly_IsDataAddress() is *inclusive* //
                             // KEEP IN SYNC:  CmdDefineByteRange()
                             // CmdDisasmDataList() GetOpmodeOpbyte()
                             // FormatNopcodeBytes()
      );
    }
  }

  return UPDATE_DISASM | ConsoleUpdate();
}

// Common code
//===========================================================================
auto CmdDisasmDataDefByteX(int nArgs) -> UpdateResult {
  // DB
  // DB symbol // use current instruction pointer
  // DB symbol address
  // DB symbol range:range
  // DB address
  // To "return to code" use ."X"
  int iCmd = args[0].nValue - NOP_BYTE_1;

  if (nArgs > 4)  // 2.7.0.31 Bug fix: DB range, i.e. DB 174E:174F
  {
    return Help_Arg_1(CMD_DEFINE_DATA_BYTE1 + iCmd);
  }

  DisasmData tData{};
  int iArg = 2;

  if ((nArgs == 3) &&
      (args[2].eToken ==
       TOKEN_COLON))  // 2.7.0.31 Bug fix: DB range, i.e. DB 174E:175F

  {
    iArg = 1;
  }

  uint16_t address = CmdDefineByteRange(nArgs, iArg, tData);

  // TODO: Allow user to select which assembler to use for displaying
  // directives!
  //	tData.iDirective = FIRST_M_DIRECTIVE + ASM_M_DEFINE_BYTE;
  tData.iDirective =
      assembler_first_directive[assembler_syntax] + ASM_DEFINE_BYTE;

  tData.eElementType = static_cast<Nopcode>(NOP_BYTE_1 + iCmd);
  tData.bSymbolLookup = 0;
  tData.nTargetAddress = 0;

  // Already exists, so update
  DisasmData* data = Disassembly_IsDataAddress(address);
  if (data) {
    *data = tData;
  } else {
    Disassembly_AddData(tData);
  }

  return UPDATE_DISASM | ConsoleUpdate();
}

/*
        Usage:
                DW
                DW symbol
                DW symbol address
                DW symbol range:range
                DW address  Auto-define W_#### where # is the address
                DW range    Auto-define W_#### where # is the address
        Examples:
                DW 3F2:3F3
*/
//===========================================================================
auto CmdDisasmDataDefWordX(int nArgs) -> UpdateResult {
  int iCmd = args[0].nValue - NOP_WORD_1;

  if (nArgs > 4)  // 2.7.0.31 Bug fix: DB range, i.e. DB 174E:174F
  {
    return Help_Arg_1(CMD_DEFINE_DATA_WORD1 + iCmd);
  }

  DisasmData tData{};
  int iArg = 2;

  if ((nArgs == 3) &&
      (args[2].eToken ==
       TOKEN_COLON))  // 2.7.0.33 Bug fix: DW range, i.e. DW 3F2:3F3

  {
    iArg = 1;
  }

  uint16_t address = CmdDefineByteRange(nArgs, iArg, tData);

  //	tData.iDirective = FIRST_M_DIRECTIVE + ASM_M_DEFINE_WORD;
  tData.iDirective =
      assembler_first_directive[assembler_syntax] + ASM_DEFINE_WORD;

  tData.eElementType = static_cast<Nopcode>(NOP_WORD_1 + iCmd);
  tData.bSymbolLookup = 0;
  tData.nTargetAddress = 0;

  // Already exists, so update
  DisasmData* data = Disassembly_IsDataAddress(address);
  if (data) {
    *data = tData;
  } else {
    Disassembly_AddData(tData);
  }

  return UPDATE_DISASM | ConsoleUpdate();
}

//===========================================================================
auto CmdDisasmDataDefAddress8H(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdDisasmDataDefAddress8L(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdDisasmDataDefAddress16(int nArgs) -> UpdateResult {
  int iCmd = NOP_WORD_1 - args[0].nValue;

  if ((nArgs > 2) && (nArgs != 4)) {
    return Help_Arg_1(CMD_DEFINE_DATA_WORD1 + iCmd);
  }

  DisasmData tData{};
  int iArg = 2;
  uint16_t address = CmdDefineByteRange(nArgs, iArg, tData);

  //	tData.iDirective = FIRST_M_DIRECTIVE + ASM_M_DEFINE_WORD;
  tData.iDirective =
      assembler_first_directive[assembler_syntax] + ASM_DEFINE_ADDRESS_16;

  tData.eElementType = NOP_ADDRESS;
  tData.bSymbolLookup = 1;
  tData.nTargetAddress = 0;  // dynamic -- will be filled in ...

  // Already exists, so update
  DisasmData* data = Disassembly_IsDataAddress(address);
  if (data) {
    *data = tData;
  } else {
    Disassembly_AddData(tData);
  }

  return UPDATE_DISASM | ConsoleUpdate();
}

// DB
auto CmdDisasmDataDefByte1(int nArgs) -> UpdateResult {
  args[0].nValue = NOP_BYTE_1;
  return CmdDisasmDataDefByteX(nArgs);
}

// DB2
auto CmdDisasmDataDefByte2(int nArgs) -> UpdateResult {
  args[0].nValue = NOP_BYTE_2;
  return CmdDisasmDataDefByteX(nArgs);
}

auto CmdDisasmDataDefByte4(int nArgs) -> UpdateResult {
  args[0].nValue = NOP_BYTE_4;
  return CmdDisasmDataDefByteX(nArgs);
}

auto CmdDisasmDataDefByte8(int nArgs) -> UpdateResult {
  args[0].nValue = NOP_BYTE_8;
  return CmdDisasmDataDefByteX(nArgs);
}

// DW
auto CmdDisasmDataDefWord1(int nArgs) -> UpdateResult {
  args[0].nValue = NOP_WORD_1;
  return CmdDisasmDataDefWordX(nArgs);
}

// DW2
auto CmdDisasmDataDefWord2(int nArgs) -> UpdateResult {
  args[0].nValue = NOP_WORD_2;
  return CmdDisasmDataDefWordX(nArgs);
}

auto CmdDisasmDataDefWord4(int nArgs) -> UpdateResult {
  args[0].nValue = NOP_WORD_4;
  return CmdDisasmDataDefWordX(nArgs);
}

// Command: DS
//		ASC range    Auto-define T_#### where # is the address
auto CmdDisasmDataDefString(int nArgs) -> UpdateResult {
  int iCmd = 0;  // Define Ascii, AppleText, MixedText (DOS3.3)

  if (nArgs > 4)  // 2.7.0.31 Bug fix: DB range, i.e. DB 174E:174F
  {
    return Help_Arg_1(CMD_DEFINE_DATA_STR + iCmd);
  }

  DisasmData tData{};
  int iArg = 2;

  if ((nArgs == 3) &&
      (args[2].eToken ==
       TOKEN_COLON))  // 2.7.0.32 Bug fix: ASC range, i.e. ASC 174E:175F

  {
    iArg = 1;
  }

  uint16_t address = CmdDefineByteRange(nArgs, iArg, tData);

  //	tData.iDirective = assembler_first_directive[ assembler_syntax ] +
  // ASM_DEFINE_APPLE_TEXT;
  tData.iDirective = FIRST_MERLIN_DIRECTIVE + ASM_MERLIN_ASCII;

  tData.eElementType = static_cast<Nopcode>(NOP_STRING_APPLE + iCmd);
  tData.bSymbolLookup = 0;
  tData.nTargetAddress = 0;

  // Already exists, so update
  DisasmData* data = Disassembly_IsDataAddress(address);
  if (data) {
    *data = tData;
  } else {
    Disassembly_AddData(tData);
  }

  return UPDATE_DISASM | ConsoleUpdate();
}

// __ Disassembler View Interface
// ____________________________________________________________________

/// @param pCurrent nullptr start a new search, or continue enumerating
//===========================================================================
auto Disassembly_Enumerate(DisasmData* pCurrent) -> DisasmData* {
  DisasmData* data = nullptr;  // bIsNopcode = false
  int nDataTargets = disassembler_data.size();

  if (nDataTargets != 0) {
    DisasmData* pBegin = disassembler_data.data();
    DisasmData* pEnd = &disassembler_data[nDataTargets - 1];

    if (pCurrent) {
      pCurrent++;
      if (pCurrent <= pEnd) {
        data = pCurrent;
      }
    } else {
      data = pBegin;
    }
  }
  return data;
}

// returns nullptr if address has no data associated with it
//===========================================================================
auto Disassembly_IsDataAddress(uint16_t address) -> DisasmData* {
  DisasmData* data = nullptr;  // bIsNopcode = false
  int nDataTargets = disassembler_data.size();

  if (nDataTargets != 0) {
    // TODO: Replace with binary search -- should store data in sorted order,
    // via start address
    data = disassembler_data.data();
    for (int iTarget = 0; iTarget < nDataTargets; iTarget++) {
      if ((data->iDirective != NOP_REMOVED) &&
          ((address >= data->nStartAddress) &&
           (address <= data->nEndAddress))) {
        return data;
      }

      data++;
    }
    data = nullptr;  // bIsNopCode = false
  }
  return data;
}

// Notes: tData.iDirective should not be NOP_REMOVED !
//===========================================================================
auto Disassembly_AddData(DisasmData tData) -> void {
  disassembler_data.push_back(tData);
}

// DEPRECATED ! Inlined in GetOpmodeOpbyte() !
//===========================================================================
auto Disassembly_GetData(uint16_t nBaseAddress, const DisasmData* data,
                         DisasmLine& line_) -> void {
  (void)nBaseAddress;
  (void)line_;
  if (!data) {
    return;
  }
}

//===========================================================================
auto Disassembly_DelData(DisasmData tData) -> void {
  // disassembler_data.erase( );
  uint16_t address = tData.nStartAddress;

  DisasmData* data = nullptr;  // bIsNopcode = false
  int nDataTargets = disassembler_data.size();

  if (nDataTargets != 0) {
    // TODO: Replace with binary search -- should store data in sorted order,
    // via start address
    data = disassembler_data.data();
    for (int iTarget = 0; iTarget < nDataTargets; iTarget++) {
      if ((data->iDirective != NOP_REMOVED) &&
          ((address >= data->nStartAddress) && (address < data->nEndAddress))) {
        data->iDirective = NOP_REMOVED;
      }

      data++;
    }
    data = nullptr;  // bIsNopCode = false
  }
}
