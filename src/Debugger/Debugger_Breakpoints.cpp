// SPDX-License-Identifier: GPL-2.0-only
#include "Debugger_Breakpoints.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

#include "Debug.h"
#include "Debugger_Cmd_Config.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"
#include "core/Util_Text.h"
extern const Opcodes* opcodes;
extern const Opcodes opcodes65_c02[NUM_OPCODES];

int debug_break_on_invalid = 0;  // Bit Flags of Invalid Opcode to break on
int debug_break_on_opcode = 0;

int debug_breakpoint_hit = 0;  // See: BreakpointHit

int breakpoints_count = 0;
Breakpoint breakpoints[MAX_BREAKPOINTS] = {};

// NOTE: BreakpointSource and breakpoint_source must match!
const char* breakpoint_source[NUM_BREAKPOINT_SOURCES] = {
    "A", "X", "Y", "PC", "S", "P",  "C", "Z", "I",
    "D", "B", "R", "V",  "N", "OP", "M", "M", "M",
};

// Note: BreakpointOperator, PARAM_BREAKPOINT_, and breakpoint_symbols must
// stay in sync!
const char* breakpoint_symbols[NUM_BREAKPOINT_OPERATORS] = {
    "<=", "< ", "= ", "!=", "> ", ">=", "? ", "@ ", "* ",
};

auto IsDebugBreakOnInvalid(int iOpcodeType) -> bool {
  debug_breakpoint_hit |=
      (((debug_break_on_invalid >> iOpcodeType) & 1) != 0) ? BP_HIT_INVALID
                                                             : 0;
  return debug_breakpoint_hit != 0;
}

auto ClearTempBreakpoints() -> void {
  int iBP = 0;
  while (iBP < MAX_BREAKPOINTS) {
    if (breakpoints[iBP].bSet && breakpoints[iBP].bTemp) {
      bwz_Clear(breakpoints, iBP);
      breakpoints_count--;
    }
    iBP++;
  }
}

// BWZ (Breakpoint, Watch, ZeroPage) shared helpers
// _______________________________________________

auto bwz_Clear(Breakpoint* aBreakWatchZero, int iSlot) -> void {
  if (aBreakWatchZero) {
    aBreakWatchZero[iSlot].bSet = false;
    aBreakWatchZero[iSlot].bEnabled = false;
    aBreakWatchZero[iSlot].bTemp = false;
    aBreakWatchZero[iSlot].address = 0;
    aBreakWatchZero[iSlot].nLength = 0;
    aBreakWatchZero[iSlot].eSource = static_cast<BreakpointSource>(0);
    aBreakWatchZero[iSlot].eOperator = static_cast<BreakpointOperator>(0);
  }
}

auto bwz_RemoveOne(Breakpoint* aBreakWatchZero, const int iSlot, int& total)
    -> void {
  if (aBreakWatchZero && aBreakWatchZero[iSlot].bSet) {
    bwz_Clear(aBreakWatchZero, iSlot);
    total--;
  }
}

auto bwz_RemoveAll(Breakpoint* aBreakWatchZero, const int nMax, int& total)
    -> void {
  if (aBreakWatchZero) {
    int i = 0;
    while (i < nMax) {
      bwz_Clear(aBreakWatchZero, i);
      i++;
    }
    total = 0;
  }
}

auto bwz_ClearViaArgs(int nArgs, Breakpoint* aBreakWatchZero, const int nMax,
                      int& total) -> void {
  if (aBreakWatchZero) {
    for (int iArg = 1; iArg <= nArgs; iArg++) {
      int iSlot = args[iArg].nValue;
      if (iSlot < nMax) {
        bwz_RemoveOne(aBreakWatchZero, iSlot, total);
      }
    }
  }
}

auto bwz_EnableDisableViaArgs(int nArgs, Breakpoint* aBreakWatchZero,
                              const int nMax, const bool bEnabled) -> void {
  if (aBreakWatchZero) {
    for (int iArg = 1; iArg <= nArgs; iArg++) {
      int iSlot = args[iArg].nValue;
      if (iSlot < nMax) {
        aBreakWatchZero[iSlot].bEnabled = bEnabled;
      }
    }
  }
}

auto bwz_List(const Breakpoint* aBreakWatchZero, const int iBWZ) -> void {
  if (aBreakWatchZero) {
    char sText[CONSOLE_WIDTH];
    const Breakpoint* pBWZ = &aBreakWatchZero[iBWZ];

    const char* src_ptr = breakpoint_source[pBWZ->eSource];
    const char* pCmp = breakpoint_symbols[pBWZ->eOperator];

    snprintf(sText, sizeof(sText), "  %x: %s %s %04X", iBWZ, src_ptr, pCmp,
             pBWZ->address);
    if (pBWZ->nLength > 1) {
      char sLen[32];
      snprintf(sLen, sizeof(sLen), ",%04X", pBWZ->nLength);
      util_safe_strncat(sText, sLen, sizeof(sText));
    }

    if (!pBWZ->bEnabled) {
      util_safe_strncat(sText, " (Disabled)", sizeof(sText));
    }

    ConsoleBufferPush(sText);
  }
}

auto bwz_ListAll(const Breakpoint* aBreakWatchZero, const int nMax) -> void {
  if (aBreakWatchZero) {
    int i = 0;
    while (i < nMax) {
      if (aBreakWatchZero[i].bSet) {
        bwz_List(aBreakWatchZero, i);
      }
      i++;
    }
  }
}

// Breakpoints
// ____________________________________________________________________________________

auto CmdBreakpoint(int nArgs) -> UpdateResult {
  return CmdBreakpointAddSmart(nArgs);
}

auto CmdBreakpointAddSmart(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    return CmdBreakpointList(0);
  }

  // 1. BP address
  // 2. BP register operator value
  // 3. BP register operator value,length

  if (nArgs == 1) {
    return CmdBreakpointAddPC(nArgs);
  }

  // Check if arg[1] is a register
  int iSrc = 0;
  if (FindParam(args[1].sArg, MATCH_EXACT, iSrc, PARAM_BREAKPOINT_BEGIN,
                PARAM_BREAKPOINT_END) > 0) {
    return CmdBreakpointAddReg(nArgs);
  }

  return CmdBreakpointAddPC(nArgs);
}

auto CmdBreakpointAddPC(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    return Help_Arg_1(CMD_BREAKPOINT_ADD_PC);
  }

  for (int iArg = 1; iArg <= nArgs; iArg++) {
    uint16_t address = args[iArg].nValue;
    CmdBreakpointAddCommonArg(iArg, nArgs, BP_SRC_REG_PC, BP_OP_EQUAL);
    (void)address;
  }

  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddReg(int nArgs) -> UpdateResult {
  if (nArgs < 3) {
    return Help_Arg_1(CMD_BREAKPOINT_ADD_REG);
  }

  CmdBreakpointAddCommonArg(1, nArgs, BP_SRC_REG_A, BP_OP_EQUAL);

  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddCommonArg(int iArg, int nArg, BreakpointSource iSrc,
                               BreakpointOperator iCmp,
                               bool bIsTempBreakpoint) -> int {
  (void)nArg;
  int iBP = 0;
  while ((iBP < MAX_BREAKPOINTS) && breakpoints[iBP].bSet) {
    iBP++;
  }

  if (iBP >= MAX_BREAKPOINTS) {
    console_display_error("All breakpoints are currently in use.");
    return 0;
  }

  Breakpoint* pBP = &breakpoints[iBP];
  pBP->bSet = true;
  pBP->bEnabled = true;
  pBP->bTemp = bIsTempBreakpoint;
  pBP->eSource = iSrc;
  pBP->eOperator = iCmp;
  pBP->address = args[iArg].nValue;
  pBP->nLength = 1;

  breakpoints_count++;

  return 1;
}

auto CmdBreakpointAddIO(int nArgs) -> UpdateResult {
  if (nArgs < 1) {
    return Help_Arg_1(CMD_BREAKPOINT_ADD_IO);
  }
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddMemA(int nArgs) -> UpdateResult {
  if (nArgs < 1) {
    return Help_Arg_1(CMD_BREAKPOINT_ADD_MEM);
  }
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddMemR(int nArgs) -> UpdateResult {
  if (nArgs < 1) {
    return Help_Arg_1(CMD_BREAKPOINT_ADD_MEMR);
  }
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddMemW(int nArgs) -> UpdateResult {
  if (nArgs < 1) {
    return Help_Arg_1(CMD_BREAKPOINT_ADD_MEMW);
  }
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointEdit(int nArgs) -> UpdateResult {
  if (nArgs < 1) {
    return Help_Arg_1(CMD_BREAKPOINT_EDIT);
  }
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointClear(int nArgs) -> UpdateResult {
  if (breakpoints_count == 0) {
    return console_display_error("There are no breakpoints defined.");
  }

  if (nArgs == 0) {
    bwz_RemoveAll(breakpoints, MAX_BREAKPOINTS, breakpoints_count);
  } else {
    bwz_ClearViaArgs(nArgs, breakpoints, MAX_BREAKPOINTS,
                     breakpoints_count);
  }

  return UPDATE_DISASM | UPDATE_BREAKPOINTS | UPDATE_CONSOLE_DISPLAY;
}

auto CmdBreakpointDisable(int nArgs) -> UpdateResult {
  if (breakpoints_count == 0) {
    return console_display_error("There are no breakpoints defined.");
  }

  if (nArgs == 0) {
    return Help_Arg_1(CMD_BREAKPOINT_DISABLE);
  }

  bwz_EnableDisableViaArgs(nArgs, breakpoints, MAX_BREAKPOINTS, false);

  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointEnable(int nArgs) -> UpdateResult {
  if (breakpoints_count == 0) {
    return console_display_error("There are no breakpoints defined.");
  }

  if (nArgs == 0) {
    return Help_Arg_1(CMD_BREAKPOINT_ENABLE);
  }

  bwz_EnableDisableViaArgs(nArgs, breakpoints, MAX_BREAKPOINTS, true);

  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointList(int nArgs) -> UpdateResult {
  (void)nArgs;
  if (breakpoints_count == 0) {
    char sText[CONSOLE_WIDTH];
    snprintf(sText, sizeof(sText),
             "  There are no current breakpoints.  (Max: %d)", MAX_BREAKPOINTS);
    ConsoleBufferPush(sText);
  } else {
    bwz_ListAll(breakpoints, MAX_BREAKPOINTS);
  }
  return ConsoleUpdate();
}

auto CmdBreakpointSave(int nArgs) -> UpdateResult {
  char sText[CONSOLE_WIDTH];

  // ConfigSave_PrepareHeader( PARAM_CAT_BREAKPOINTS, CMD_BREAKPOINT_CLEAR );

  int iBreakpoint = 0;
  while (iBreakpoint < MAX_BREAKPOINTS) {
    if (breakpoints[iBreakpoint].bSet) {
      snprintf(sText, sizeof(sText), "%s %x %04X,%04X\n",
               commands[CMD_BREAKPOINT_ADD_REG].name, iBreakpoint,
               breakpoints[iBreakpoint].address,
               breakpoints[iBreakpoint].nLength);
      config_state.PushLine(sText);
    }
    if (!breakpoints[iBreakpoint].bEnabled) {
      snprintf(sText, sizeof(sText), "%s %x\n",
               commands[CMD_BREAKPOINT_DISABLE].name, iBreakpoint);
      config_state.PushLine(sText);
    }

    iBreakpoint++;
  }

  if (nArgs != 0) {
    if ((args[1].bType & TYPE_QUOTED_2) == 0) {
      return Help_Arg_1(CMD_BREAKPOINT_SAVE);
    }

    // if (ConfigSave_BufferToDisk( args[ 1 ].sArg, CONFIG_SAVE_FILE_CREATE ))
    {
      ConsoleBufferPush("Saved.");
      return ConsoleUpdate();
    }
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdWatch(int nArgs) -> UpdateResult { return CmdWatchAdd(nArgs); }

auto CmdWatchAdd(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    return CmdWatchList(0);
  }

  int iArg = 1;
  int iWatch = NO_6502_TARGET;
  if (nArgs > 1) {
    iWatch = static_cast<int>(args[1].nValue);
    iArg++;
  }

  bool bAdded = false;
  for (; iArg <= nArgs; iArg++) {
    uint16_t address = args[iArg].nValue;

    if ((address >= DBG_6502_IO_BEGIN) && (address <= DBG_6502_IO_END)) {
      return console_display_error("You may not watch an I/O location.");
    }

    if (iWatch == NO_6502_TARGET) {
      iWatch = 0;
      while ((iWatch < MAX_WATCHES) && watches[iWatch].bSet) {
        iWatch++;
      }
    }

    if ((iWatch >= MAX_WATCHES) && !bAdded) {
      char sText[CONSOLE_WIDTH];
      snprintf(sText, sizeof(sText),
               "All watches are currently in use.  (Max: %d)", MAX_WATCHES);
      ConsoleDisplayPush(sText);
      return ConsoleUpdate();
    }

    if ((iWatch < MAX_WATCHES) && (watches_count < MAX_WATCHES)) {
      watches[iWatch].bSet = true;
      watches[iWatch].bEnabled = true;
      watches[iWatch].address = address;
      bAdded = true;
      watches_count++;
      iWatch++;
    }
  }

  if (!bAdded) {
    return Help_Arg_1(CMD_WATCH_ADD);
  }

  return UPDATE_WATCH;
}

auto CmdWatchSave(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdWatchClear(int nArgs) -> UpdateResult {
  if (watches_count == 0) {
    return console_display_error("There are no watches defined.");
  }

  if (nArgs == 0) {
    bwz_RemoveAll((Breakpoint*)watches, MAX_WATCHES, watches_count);
  } else {
    bwz_ClearViaArgs(nArgs, (Breakpoint*)watches, MAX_WATCHES,
                     watches_count);
  }

  return UPDATE_WATCH | UPDATE_CONSOLE_DISPLAY;
}

auto CmdWatchDisable(int nArgs) -> UpdateResult {
  if (watches_count == 0) {
    return console_display_error("There are no watches defined.");
  }

  if (nArgs == 0) {
    return Help_Arg_1(CMD_WATCH_DISABLE);
  }

  bwz_EnableDisableViaArgs(nArgs, (Breakpoint*)watches, MAX_WATCHES, false);

  return UPDATE_WATCH;
}

auto CmdWatchEnable(int nArgs) -> UpdateResult {
  if (watches_count == 0) {
    return console_display_error("There are no watches defined.");
  }

  if (nArgs == 0) {
    return Help_Arg_1(CMD_WATCH_ENABLE);
  }

  bwz_EnableDisableViaArgs(nArgs, (Breakpoint*)watches, MAX_WATCHES, true);

  return UPDATE_WATCH;
}

auto CmdWatchList(int nArgs) -> UpdateResult {
  (void)nArgs;
  if (watches_count == 0) {
    char sText[CONSOLE_WIDTH];
    snprintf(sText, sizeof(sText), "  There are no current watches.  (Max: %d)",
             MAX_WATCHES);
    ConsoleBufferPush(sText);
  } else {
    bwz_ListAll((Breakpoint*)watches, MAX_WATCHES);
  }
  return ConsoleUpdate();
}
