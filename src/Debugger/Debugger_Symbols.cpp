// SPDX-License-Identifier: GPL-2.0-only
/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2014, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Debugger Symbol Tables
 *
 * Author: Copyright (C) 2006-2010 Michael Pohoreski
 */

#include "Debugger_Symbols.h"

#include <strings.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"
#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

// 2.6.2.13 Added: Can now enable/disable selected symbol table(s) !
// Allow the user to disable/enable symbol tables
// xxx1xxx symbol table is active (are displayed in disassembly window, etc.)
// xxx1xxx symbol table is disabled (not displayed in disassembly window, etc.)
// See: CmdSymbolsListTable(), g_display_symbol_tables
int g_display_symbol_tables =
    ((1 << NUM_SYMBOL_TABLES) - 1) &
    (~static_cast<int>(
        SYMBOL_TABLE_PRODOS));  // default to all symbol tables displayed/active

// Symbols
// ________________________________________________________________________________________

const char* g_file_name_symbols[NUM_SYMBOL_TABLES] = {
    "APPLE2E.SYM",
    "A2_BASIC.SYM",
    "A2_ASM.SYM",
    "A2_USER1.SYM"  // "A2_USER.SYM",
    ,
    "A2_USER2.SYM",
    "A2_SRC1.SYM"  // "A2_SRC.SYM",
    ,
    "A2_SRC2.SYM",
    "A2_DOS33.SYM",
    "A2_PRODOS.SYM"};
std::string g_file_name_symbols_user;

const char* g_symbol_table_names[NUM_SYMBOL_TABLES] = {"Main",
                                                       "Basic",
                                                       "Asm"  // "Assembly",
                                                       ,
                                                       "User1"  // User
                                                       ,
                                                       "User2",
                                                       "Src1",
                                                       "Src2",
                                                       "DOS33",
                                                       "ProDOS"};

bool g_symbols_display_missing_file = true;

SymbolTable_t g_symbols[NUM_SYMBOL_TABLES];
int g_symbols_loaded = 0;  // on Last Load

// Utils _
// ________________________________________________________________________________________

auto CmdSymbolsInfoHeader(int iTable, char* text, size_t text_size,
                          int nDisplaySize = 0) -> void;
auto PrintCurrentPath() -> void;
auto PrintSymbolInvalidTable() -> Update_t;

// Private
// ________________________________________________________________________________________

//===========================================================================
auto PrintCurrentPath() -> void {
  console_display_error(g_state.program_dir.data());
}

auto PrintSymbolInvalidTable() -> Update_t {
  char sText[CONSOLE_WIDTH * 2];
  char sTemp[CONSOLE_WIDTH * 2];

  // TODO: display the user specified file name
  ConsoleBufferPush("Invalid symbol table.");

  ConsolePrintFormat(sText,
                     "Only %s%d%s symbol tables are supported:", CHC_NUM_DEC,
                     NUM_SYMBOL_TABLES, CHC_DEFAULT);

  // Similar to CmdSymbolsInfoHeader()
  sText[0] = 0;
  for (int iTable = 0; iTable < NUM_SYMBOL_TABLES; iTable++) {
    snprintf(sTemp, sizeof(sTemp), "%s%s%s%c "  // %s"
             ,
             CHC_USAGE, g_symbol_table_names[iTable], CHC_ARG_SEP,
             (iTable != (NUM_SYMBOL_TABLES - 1)) ? ',' : '.');
    util_safe_strncat(sText, sTemp, sizeof(sText));
  }

  //	return console_display_error( sText );
  console_print(sText);
  return ConsoleUpdate();
}

// Public
// _________________________________________________________________________________________

auto GetSymbol(uint16_t address, int nBytes) -> const char* {
  const char* pSymbol = FindSymbolFromAddress(address);
  if (pSymbol) {
    return pSymbol;
  }

  return FormatAddress(address, nBytes);
}

auto GetSymbolTableFromCommand() -> int {
  return (g_command - CMD_SYMBOLS_ROM);
}

auto FindSymbolFromAddress(uint16_t address, int* iTable_) -> const char* {
  // Bugfix/User feature: User symbols should be searched first
  int iTable = NUM_SYMBOL_TABLES;
  while (iTable-- > 0) {
    if (!g_symbols[iTable].size()) {
      continue;
    }

    if (!(g_display_symbol_tables & (1 << iTable))) {
      continue;
    }

    auto iSymbols = g_symbols[iTable].find(address);
    if (g_symbols[iTable].find(address) != g_symbols[iTable].end()) {
      if (iTable_) {
        *iTable_ = iTable;
      }
      return iSymbols->second.c_str();
    }
  }
  return nullptr;
}

//===========================================================================
auto FindAddressFromSymbol(const char* pSymbol, uint16_t* pAddress_,
                           int* iTable_) -> bool {
  // Bugfix/User feature: User symbols should be searched first
  for (int iTable = NUM_SYMBOL_TABLES; iTable-- > 0;) {
    if (!g_symbols[iTable].size()) {
      continue;
    }

    if (!(g_display_symbol_tables & (1 << iTable))) {
      continue;
    }

    auto iSymbol = g_symbols[iTable].begin();
    while (iSymbol != g_symbols[iTable].end()) {
      if (!strcasecmp(iSymbol->second.c_str(), pSymbol)) {
        if (pAddress_) {
          *pAddress_ = iSymbol->first;
        }
        if (iTable_) {
          *iTable_ = iTable;
        }
        return true;
      }
      iSymbol++;
    }
  }
  return false;
}

// Symbols
// ________________________________________________________________________________________

//===========================================================================
auto GetAddressFromSymbol(const char* pSymbol) -> uint16_t {
  uint16_t address = 0;
  bool bFoundSymbol = FindAddressFromSymbol(pSymbol, &address);
  if (!bFoundSymbol) {
    address = 0;
  }
  return address;
}

auto String2Address(const char* text, uint16_t& nAddress_) -> bool {
  char sHexApple[CONSOLE_WIDTH];

  if (text[0] == '$') {
    if (!text_is_hex_string(text + 1)) {
      return false;
    }

    util_safe_strcpy(sHexApple, "0x", sizeof(sHexApple));
    util_safe_strcpy(sHexApple + 2, text + 1, MAX_SYMBOLS_LEN - 3);
    text = sHexApple;
  }

  if (text[0] == '0') {
    if ((text[1] == 'X') || text[1] == 'x') {
      if (!text_is_hex_string(text + 2)) {
        return false;
      }

      char* pEnd = nullptr;
      nAddress_ = static_cast<uint16_t>(strtol(text, &pEnd, 16));
      return true;
    }
    if (text_is_hex_string(text)) {
      char* pEnd = nullptr;
      nAddress_ = static_cast<uint16_t>(strtol(text, &pEnd, 16));
      return true;
    }
  }

  return false;
}

//===========================================================================
auto CmdSymbols(int nArgs) -> Update_t {
  if (!nArgs) {
    return CmdSymbolsInfo(0);
  }

  Update_t iUpdate = CmdSymbolsUpdate(nArgs, SYMBOL_TABLE_USER_1);
  if (iUpdate != UPDATE_NOTHING) {
    return iUpdate;
  }

  int bSymbolTables = (1 << NUM_SYMBOL_TABLES) - 1;
  return CmdSymbolsListTables(nArgs, bSymbolTables);
}

//===========================================================================
auto CmdSymbolsClear(int nArgs) -> Update_t {
  (void)nArgs;
  SymbolTable_Index_e eSymbolTable = SYMBOLS_USER_1;
  CmdSymbolsClear(eSymbolTable);
  return (UPDATE_DISASM | UPDATE_SYMBOLS);
}

// Format the summary of the specified symbol table
//===========================================================================
auto CmdSymbolsInfoHeader(int iTable, char* text, size_t text_size,
                          int nDisplaySize = 0) -> void {
  // Common case is to use/calc the table size
  bool bActive = (g_display_symbol_tables & (1 << iTable)) != 0;
  int nSymbols = nDisplaySize ? nDisplaySize : g_symbols[iTable].size();

  // Short Desc: `MAIN`: `1000`
  // // 2.6.2.19 Color for name of symbol table: CmdPrintSymbol() "SYM HOME"
  // CmdSymbolsInfoHeader "SYM" CHC_STRING and CHC_NUM_DEC are both cyan, using
  // CHC_USAGE instead of CHC_STRING
  snprintf(text, text_size, "%s%s%s:%s%d ", CHC_USAGE,
           g_symbol_table_names[iTable], CHC_ARG_SEP,
           bActive ? CHC_NUM_DEC : CHC_WARNING, nSymbols);
}

//===========================================================================
auto CmdSymbolsInfo(int nArgs) -> Update_t {
  const char sIndent[] = "  ";
  char sText[CONSOLE_WIDTH * 4] = "";
  char sTemp[CONSOLE_WIDTH * 2] = "";

  int bDisplaySymbolTables = 0;

  util_safe_strcpy(sText, sIndent, sizeof(sText));  // Indent new line

  if (!nArgs) {
    // default to all tables
    bDisplaySymbolTables = (1 << NUM_SYMBOL_TABLES) - 1;
  } else {  // Convert Command Index to parameter
    int iWhichTable = GetSymbolTableFromCommand();
    if ((iWhichTable < 0) || (iWhichTable >= NUM_SYMBOL_TABLES)) {
      return PrintSymbolInvalidTable();
    }

    bDisplaySymbolTables = (1 << iWhichTable);
  }

  // sprintf( sText, "  Symbols  Main: %s%d%s  User: %s%d%s   Source: %s%d%s"
  //  "Main:# Basic:# Asm:# User1:# User2:# Src1:# Src2:# Dos:# Prodos:#

  int bTable = 1;
  int iTable = 0;
  for (; bTable <= bDisplaySymbolTables; iTable++, bTable <<= 1) {
    if (bDisplaySymbolTables & bTable) {
      CmdSymbolsInfoHeader(iTable, sTemp, sizeof(sTemp));  // 15 chars per table

      // 2.8.0.4 BUGFIX: Check for buffer overflow and wrap text
      int nLen = ConsoleColor_StringLength(sTemp);
      int nDst = ConsoleColor_StringLength(sText);
      if ((nDst + nLen) > CONSOLE_WIDTH) {
        console_print(sText);
        util_safe_strcpy(sText, sIndent, sizeof(sText));  // Indent new line
      }
      util_safe_strncat(sText, sTemp, sizeof(sText));
    }
  }
  console_print(sText);

  return ConsoleUpdate();
}

//===========================================================================
auto CmdPrintSymbol(const char* pSymbol, uint16_t address, int iTable) -> void {
  char sText[CONSOLE_WIDTH * 2];

  // 2.6.2.19 Color for name of symbol table: CmdPrintSymbol() "SYM HOME"
  // CmdSymbolsInfoHeader "SYM" CHC_STRING and CHC_NUM_DEC are both cyan, using
  // CHC_USAGE instead of CHC_STRING

  // 2.6.2.20 Changed: Output of found symbol more table friendly.  Symbol table
  // name displayed first.
  ConsolePrintFormat(sText, "  %s%s%s: $%s%04X %s%s", CHC_USAGE,
                     g_symbol_table_names[iTable], CHC_ARG_SEP, CHC_ADDRESS,
                     address, CHC_SYMBOL, pSymbol);

  // ConsoleBufferPush( sText );
}

// Test if bit-mask to index (equal to number of bit-shifs required to reach
// table)
//=========================================================================== */
auto FindSymbolTable(int bSymbolTables, int iTable) -> bool {
  // iTable is enumeration
  // bSymbolTables is bit-flags of enabled tables to search

  return (bSymbolTables & (1 << iTable)) != 0;
}

// Convert bit-mask to index
//=========================================================================== */
auto GetSymbolTableFromFlag(int bSymbolTables) -> int {
  int iTable = 0;
  int bTable = 1;

  for (; bTable <= bSymbolTables; iTable++, bTable <<= 1) {
    if (bTable & bSymbolTables) {
      break;
    }
  }

  return iTable;
}

/**
        @param bSymbolTables Bit Flags of which symbol tables to search
//=========================================================================== */
auto CmdSymbolList_Address2Symbol(int address, int bSymbolTables) -> bool {
  int iTable = 0;
  const char* pSymbol = FindSymbolFromAddress(address, &iTable);

  if (pSymbol) {
    if (FindSymbolTable(bSymbolTables, iTable)) {
      CmdPrintSymbol(pSymbol, address, iTable);
      return true;
    }
  }

  return false;
}

//===========================================================================
auto CmdSymbolList_Symbol2Address(const char* pSymbol, int bSymbolTables)
    -> bool {
  int iTable = 0;
  uint16_t address = 0;

  bool bFoundSymbol = FindAddressFromSymbol(pSymbol, &address, &iTable);
  if (bFoundSymbol) {
    if (FindSymbolTable(bSymbolTables, iTable)) {
      CmdPrintSymbol(pSymbol, address, iTable);
    }
  }
  return bFoundSymbol;
}

// LIST is normally an implicit "LIST *", but due to the numbers of symbols
// only look up symbols the user specifies
//===========================================================================
auto CmdSymbolsList(int nArgs) -> Update_t {
  int bSymbolTables = (1 << NUM_SYMBOL_TABLES) - 1;  // default to all
  return CmdSymbolsListTables(nArgs, bSymbolTables);
}

//===========================================================================
auto CmdSymbolsListTables(int nArgs, int bSymbolTables) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(CMD_SYMBOLS_LIST);
  }

  /*
          Test Cases

          SYM 0 RESET FA6F $FA59
                  $0000 LOC0
                  $FA6F RESET
                  $FA6F INITAN
                  $FA59 OLDBRK
          SYM B

          SYMBOL B = $2000
          SYM B
  */

  char sText[CONSOLE_WIDTH] = "";

  for (int iArgs = 1; iArgs <= nArgs; iArgs++) {
    uint16_t address = g_args[iArgs].nValue;
    const char* pSymbol = g_args[iArgs].sArg;

    // Dump all symbols for this table
    if (g_arg_raw[iArgs].eToken == TOKEN_STAR) {
      //		int iWhichTable = (g_command - CMD_SYMBOLS_MAIN);
      //		bDisplaySymbolTables = (1 << iWhichTable);

      int iTable = 0;
      int bTable = 1;
      for (; bTable <= bSymbolTables; iTable++, bTable <<= 1) {
        if (bTable & bSymbolTables) {
          int nSymbols = g_symbols[iTable].size();
          if (nSymbols) {
            auto iSymbol = g_symbols[iTable].begin();
            while (iSymbol != g_symbols[iTable].end()) {
              const char* pSymbol = iSymbol->second.c_str();
              uint16_t address = iSymbol->first;
              CmdPrintSymbol(pSymbol, address, iTable);
              ++iSymbol;
            }
          }
          CmdSymbolsInfoHeader(iTable, sText, sizeof(sText));
          console_print(sText);
        }
      }
    } else if (address) {  // Have address, do symbol lookup first
      if (!CmdSymbolList_Symbol2Address(pSymbol, bSymbolTables)) {
        // nope, ok, try as address
        if (!CmdSymbolList_Address2Symbol(address, bSymbolTables)) {
          ConsolePrintFormat(sText, " Address not found: %s$%s%04X%s",
                             CHC_ARG_SEP, CHC_ADDRESS, address, CHC_DEFAULT);
        }
      }
    } else {  // Have symbol, do address lookup
      if (!CmdSymbolList_Symbol2Address(
              pSymbol, bSymbolTables)) {  // nope, ok, try as address
        if (String2Address(pSymbol, address)) {
          if (!CmdSymbolList_Address2Symbol(address, bSymbolTables)) {
            ConsolePrintFormat(sText, " %sSymbol not found: %s%s%s", CHC_ERROR,
                               CHC_SYMBOL, pSymbol, CHC_DEFAULT);
          }
        } else {
          ConsolePrintFormat(sText, " %sSymbol not found: %s%s%s", CHC_ERROR,
                             CHC_SYMBOL, pSymbol, CHC_DEFAULT);
        }
      }
    }
  }
  return ConsoleUpdate();
}

//===========================================================================
auto ParseSymbolTable(const std::string& pPathFileName,
                      SymbolTable_Index_e eSymbolTableWrite, int nSymbolOffset)
    -> int {
  char sText[CONSOLE_WIDTH * 3];
  bool bFileDisplayed = false;

  const int nMaxLen = std::min(static_cast<int>(MAX_TARGET_LEN),
                               static_cast<int>(MAX_SYMBOLS_LEN));

  int nSymbolsLoaded = 0;

  if (pPathFileName.empty()) {
    return nSymbolsLoaded;
  }

  // #if _UNICODE
  //	char sFormat1[ MAX_SYMBOLS_LEN ];
  //	char sFormat2[ MAX_SYMBOLS_LEN ];
  //	wsprintf( sFormat1, "%%x %%%ds", MAX_SYMBOLS_LEN ); // i.e. "%x %13s"
  //	wsprintf( sFormat2, "%%%ds %%x", MAX_SYMBOLS_LEN ); // i.e. "%13s %x"
  //  ascii
  char sFormat1[MAX_SYMBOLS_LEN];
  char sFormat2[MAX_SYMBOLS_LEN];
  snprintf(sFormat1, sizeof(sFormat1), "%%x %%%ds", MAX_SYMBOLS_LEN);
  snprintf(sFormat2, sizeof(sFormat2), "%%%ds %%x", MAX_SYMBOLS_LEN);

  FilePtr_t hFile(fopen(pPathFileName.c_str(), "rt"), fclose);

  if (!hFile && g_symbols_display_missing_file) {
    // TODO: print filename! Bug #242 Help file (.chm) description for "Symbols"
    // #242
    console_display_error("Symbol File not found:");
    PrintCurrentPath();
    nSymbolsLoaded = -1;  // HACK: ERROR: FILE NOT EXIST
  }

  bool bDupSymbolHeader = false;
  if (hFile) {
    while (!feof(hFile.get())) {
      // Support 2 types of symbols files:
      // 1) AppleWin:
      //    . 0000 SYMBOL
      //    . FFFF SYMBOL
      // 2) ACME:
      //    . SYMBOL  =$0000; Comment
      //    . SYMBOL  =$FFFF; Comment
      //
      uint32_t address = APPLE2_6502_MEM_END + 1;  // default to invalid address
      char sName[MAX_SYMBOLS_LEN + 1] = "";

      const int MAX_LINE = 256;
      char line[MAX_LINE] = "";

      if (!fgets(line, MAX_LINE - 1, hFile.get()))  // Get next line
      {
        break;
      }

      if (strstr(line, "$") == nullptr) {
        sscanf(line, sFormat1, &address, sName);
      } else {
        char* p = strstr(line, "=");  // Optional
        if (p) *p = ' ';
        p = strstr(line, "$");
        if (p) *p = ' ';
        p = strstr(line, ";");  // Optional
        if (p) *p = 0;
        p = strstr(line, " ");  // 1st space between name & value
        if (p) {
          int nLen = p - line;
          if (nLen > MAX_SYMBOLS_LEN) {
            memset(&line[MAX_SYMBOLS_LEN], ' ',
                   nLen - MAX_SYMBOLS_LEN);  // sscanf fails for address if
                                             // string too long
          }
        }
        sscanf(line, sFormat2, sName, &address);
      }

      // SymbolOffset
      address += nSymbolOffset;

      if ((address > APPLE2_6502_MEM_END) || (sName[0] == 0)) {
        continue;
      }

      // If updating symbol, print duplicate symbols
      uint16_t nAddressPrev = 0;
      int iTable = 0;

      // 2.9.0.11 Bug #479
      int nLen = strlen(sName);
      if (nLen > nMaxLen) {
        ConsolePrintFormat(sText, " %sWarn.: %s%s (%d > %d)", CHC_WARNING,
                           CHC_SYMBOL, sName, nLen, nMaxLen);
        ConsoleUpdate();  // Flush buffered output so we don't ask the user to
                          // pause
      }

      // 2.8.0.5 Bug #244 (Debugger) Duplicate symbols for identical memory
      // addresses in APPLE2E.SYM
      const char* pSymbolPrev =
          FindSymbolFromAddress(static_cast<uint16_t>(address),
                                &iTable);  // don't care which table it is in
      if (pSymbolPrev) {
        if (!bFileDisplayed) {
          bFileDisplayed = true;
          ConsolePrintFormat(sText, "%s%s", CHC_PATH, pPathFileName.c_str());
        }

        ConsolePrintFormat(sText,
                           " %sInfo.: %s%-16s %saliases %s$%s%04X %s%-12s%s "
                           "(%s%s%s)"  // MAGIC NUMBER: -MAX_SYMBOLS_LEN
                           ,
                           CHC_INFO  // 2.9.0.10 was CHC_WARNING, see #479
                           ,
                           CHC_SYMBOL, sName, CHC_INFO, CHC_ARG_SEP,
                           CHC_ADDRESS, address, CHC_SYMBOL, pSymbolPrev,
                           CHC_DEFAULT, CHC_STRING,
                           g_symbol_table_names[iTable], CHC_DEFAULT);

        ConsoleUpdate();  // Flush buffered output so we don't ask the user to
                          // pause
                          /*
                                                          ConsolePrintFormat( sText, " %sWarning:
                             %sAddress already has symbol Name%s (%s%s%s): %s%s"                   , CHC_WARNING                   ,
                             CHC_INFO                   , CHC_ARG_SEP                   ,
                             CHC_STRING                   , g_symbol_table_names[ iTable ]                   ,
                             CHC_DEFAULT                   , CHC_SYMBOL                   ,
                             pSymbolPrev
                                                          );
                  
                                                          ConsolePrintFormat( sText, "  %s$%s%04X
                             %s%-31s%s"                   , CHC_ARG_SEP                   ,
                             CHC_ADDRESS                   , address                   ,
                             CHC_SYMBOL                   , sName                   , CHC_DEFAULT
                                                          );
                          */
      }

      bool bExists = FindAddressFromSymbol(sName, &nAddressPrev, &iTable);
      if (bExists) {
        if (!bDupSymbolHeader) {
          bDupSymbolHeader = true;
          ConsolePrintFormat(sText, " %sDup Symbol Name%s (%s%s%s) %s",
                             CHC_ERROR, CHC_DEFAULT, CHC_STRING,
                             g_symbol_table_names[iTable], CHC_DEFAULT,
                             pPathFileName.c_str());
        }

        ConsolePrintFormat(sText, "  %s$%s%04X %s%-31s%s", CHC_ARG_SEP,
                           CHC_ADDRESS, address, CHC_SYMBOL, sName,
                           CHC_DEFAULT);
      }

      // else // It is not a bug to have duplicate addresses by different names

      g_symbols[eSymbolTableWrite][static_cast<uint16_t>(address)] = sName;
      nSymbolsLoaded++;  // TODO: FIXME: BUG: This is the total symbols read,
                         // not added
    }
  }

  return nSymbolsLoaded;
}

//===========================================================================
auto CmdSymbolsLoad(int nArgs) -> Update_t {
  std::string sFileName = g_state.program_dir.data();

  int iSymbolTable = GetSymbolTableFromCommand();
  if ((iSymbolTable < 0) || (iSymbolTable >= NUM_SYMBOL_TABLES)) {
    return PrintSymbolInvalidTable();
  }

  int nSymbols = 0;

  // Debugger will call us with 0 args on startup as a way to pre-load symbol
  // tables
  if (!nArgs) {
    sFileName += g_file_name_symbols[iSymbolTable];
    nSymbols = ParseSymbolTable(sFileName,
                                static_cast<SymbolTable_Index_e>(iSymbolTable));
  }

  int iArg = 1;
  if (iArg <= nArgs) {
    std::string pFileName;

    if (g_args[iArg].bType & TYPE_QUOTED_2) {
      pFileName = g_args[iArg].sArg;

      sFileName = std::string(g_state.program_dir.data()) + pFileName;

      // Remember File Name of last symbols loaded
      g_file_name_symbols_user = pFileName;
    }

    // SymbolOffset
    // sym load "filename" [,symbol_offset]
    uint32_t nOffsetAddr = 0;

    iArg++;
    if (iArg <= nArgs) {
      if (g_args[iArg].eToken == TOKEN_COMMA) {
        iArg++;
        if (iArg <= nArgs) {
          nOffsetAddr = g_args[iArg].nValue;
          if ((nOffsetAddr < DBG_6502_MEM_BEGIN) ||
              (nOffsetAddr > APPLE2_6502_MEM_END)) {
            nOffsetAddr = 0;
          }
        }
      }
    }

    if (!pFileName.empty()) {
      nSymbols = ParseSymbolTable(
          sFileName, static_cast<SymbolTable_Index_e>(iSymbolTable),
          nOffsetAddr);
    }
  }

  if (nSymbols > 0) {
    g_symbols_loaded = nSymbols;
  }

  Update_t bUpdateDisplay = UPDATE_DISASM;
  bUpdateDisplay |= (nSymbols > 0) ? UPDATE_SYMBOLS : 0;

  return bUpdateDisplay;
}

//===========================================================================
auto CmdSymbolsClear(SymbolTable_Index_e eSymbolTable) -> Update_t {
  g_symbols[eSymbolTable].clear();

  return UPDATE_SYMBOLS;
}

//===========================================================================
auto SymbolUpdate(SymbolTable_Index_e eSymbolTable, const char* pSymbolName,
                  uint16_t address, bool bRemoveSymbol, bool bUpdateSymbol)
    -> void {
  if (bRemoveSymbol) {
    pSymbolName = g_args[2].sArg;
  }

  if (strlen(pSymbolName) < MAX_SYMBOLS_LEN) {
    uint16_t nAddressPrev = 0;
    int iTable = 0;
    bool bExists = FindAddressFromSymbol(pSymbolName, &nAddressPrev, &iTable);

    if (bExists) {
      if (iTable == eSymbolTable) {
        if (bRemoveSymbol) {
          ConsoleBufferPush(" Removing symbol.");
        }

        g_symbols[eSymbolTable].erase(nAddressPrev);

        if (bUpdateSymbol) {
          char sText[CONSOLE_WIDTH * 2];
          ConsolePrintFormat(sText,
                             " Updating %s%s%s from %s$%s%04X%s to %s$%s%04X%s",
                             CHC_SYMBOL, pSymbolName, CHC_DEFAULT, CHC_ARG_SEP,
                             CHC_ADDRESS, nAddressPrev, CHC_DEFAULT,
                             CHC_ARG_SEP, CHC_ADDRESS, address, CHC_DEFAULT);
        }
      }
    } else {
      if (bRemoveSymbol) {
        ConsoleBufferPush(" Symbol not in table.");
      }
    }

    if (bUpdateSymbol) {
#if _DEBUG
      const char* pSymbol = FindSymbolFromAddress(address, &iTable);
      {
        // Found another symbol for this address.  Harmless.
        // TODO: Probably should check if same name?
      }
#endif
      g_symbols[eSymbolTable][address] = pSymbolName;

      // Tell user symbol was added
      char sText[CONSOLE_WIDTH * 2];
      ConsolePrintFormat(sText, " Added symbol: %s%s%s %s$%s%04X%s", CHC_SYMBOL,
                         pSymbolName, CHC_DEFAULT, CHC_ARG_SEP, CHC_ADDRESS,
                         address, CHC_DEFAULT);
    }
  }
}

//===========================================================================
auto CmdSymbolsUpdate(int nArgs, int bSymbolTables) -> Update_t {
  bool bRemoveSymbol = false;
  bool bUpdateSymbol = false;

  if ((nArgs == 2) && ((g_args[1].eToken == TOKEN_EXCLAMATION) ||
                       (g_args[1].eToken == TOKEN_TILDE))) {
    bRemoveSymbol = true;
  }

  if ((nArgs == 3) && (g_args[2].eToken == TOKEN_EQUAL)) {
    bUpdateSymbol = true;
  }

  if (bRemoveSymbol || bUpdateSymbol) {
    char* pSymbolName = g_args[1].sArg;
    uint16_t address = g_args[3].nValue;

    int iTable = GetSymbolTableFromFlag(bSymbolTables);
    SymbolUpdate(static_cast<SymbolTable_Index_e>(iTable), pSymbolName, address,
                 bRemoveSymbol, bUpdateSymbol);
    return ConsoleUpdate();
  }

  return UPDATE_NOTHING;
}

auto CmdSymbolsCommon(int nArgs, int bSymbolTables) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(g_command);
  }

  Update_t iUpdate = CmdSymbolsUpdate(nArgs, bSymbolTables);
  if (iUpdate != UPDATE_NOTHING) {
    return iUpdate;
  }

  char sText[CONSOLE_WIDTH];

  int iArg = 0;
  while (iArg++ <= nArgs) {
    int iParam = 0;
    int nParams =
        FindParam(g_args[iArg].sArg, MATCH_EXACT, iParam);  // MATCH_FUZZY
    if (nParams) {
      if (iParam == PARAM_CLEAR) {
        int iTable = GetSymbolTableFromFlag(bSymbolTables);
        if (iTable != NUM_SYMBOL_TABLES) {
          Update_t iUpdate =
              CmdSymbolsClear(static_cast<SymbolTable_Index_e>(iTable));
          ConsolePrintFormat(sText, " Cleared symbol table: %s%s", CHC_STRING,
                             g_symbol_table_names[iTable]);
          iUpdate |= ConsoleUpdate();
          return iUpdate;
        } else {
          // Shouldn't have multiple symbol tables selected
          //					nArgs = Arg_1( eSymbolsTable );
          ConsoleBufferPush(" error: Unknown Symbol Table Type");
          return ConsoleUpdate();
        }
      } else if (iParam == PARAM_LOAD) {
        nArgs = Arg_Shift(iArg, nArgs);
        Update_t bUpdate = CmdSymbolsLoad(nArgs);

        int iTable = GetSymbolTableFromFlag(bSymbolTables);
        if (iTable != NUM_SYMBOL_TABLES) {
          if (bUpdate & UPDATE_SYMBOLS) {
            // sprintf( sText, "  Symbol Table: %s%s%s, %sloaded symbols: %s%d"
            //	, CHC_STRING, g_symbol_table_names[ iTable ]
            //	, CHC_DEFAULT, CHC_DEFAULT
            //	, CHC_NUM_DEC, g_symbols_loaded
            //);
            CmdSymbolsInfoHeader(iTable, sText, g_symbols_loaded);
            console_print(sText);
          }
        } else {
          ConsoleBufferPush(" error: Unknown Symbol Table Type");
        }
        return ConsoleUpdate();
      } else if (iParam == PARAM_SAVE) {
        nArgs = Arg_Shift(iArg, nArgs);
        return CmdSymbolsSave(nArgs);
      } else if (iParam == PARAM_ON) {
        g_display_symbol_tables |= bSymbolTables;
        int iTable = GetSymbolTableFromFlag(bSymbolTables);
        if (iTable != NUM_SYMBOL_TABLES) {
          CmdSymbolsInfoHeader(iTable, sText, sizeof(sText));
          console_print(sText);
        }
        return ConsoleUpdate() | UPDATE_DISASM;
      } else if (iParam == PARAM_OFF) {
        g_display_symbol_tables &= ~bSymbolTables;
        int iTable = GetSymbolTableFromFlag(bSymbolTables);
        if (iTable != NUM_SYMBOL_TABLES) {
          CmdSymbolsInfoHeader(iTable, sText, sizeof(sText));
          console_print(sText);
        }
        return ConsoleUpdate() | UPDATE_DISASM;
      }
    } else {
      return CmdSymbolsListTables(nArgs, bSymbolTables);
    }
  }

  return ConsoleUpdate();
}

//===========================================================================
auto CmdSymbolsCommand(int nArgs) -> Update_t {
  if (!nArgs) {
    return CmdSymbolsInfo(1);
  }

  int bSymbolTable = SYMBOL_TABLE_MAIN << GetSymbolTableFromCommand();
  return CmdSymbolsCommon(
      nArgs, bSymbolTable);  // BUGFIX 2.6.2.12 Hard-coded to SYMMAIN
}

//===========================================================================
auto CmdSymbolsSave(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}
