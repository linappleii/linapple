#include "Debugger_Breakpoints.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Range.h"
#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"

extern uint16_t g_break_memory_address;
extern MemoryTextFile_t g_config_state;
extern const Opcodes_t* g_opcodes;
extern const Opcodes_t g_opcodes65_c02[NUM_OPCODES];

extern int g_debug_break_on_opcode;
extern int g_debug_breakpoint_hit;

int g_debug_break_on_invalid = 0;  // Bit Flags of Invalid Opcode to break on
int g_debug_break_on_opcode = 0;

int g_debug_breakpoint_hit = 0;  // See: BreakpointHit_t

int g_breakpoints_count = 0;
Breakpoint_t g_breakpoints[MAX_BREAKPOINTS] = {};

// NOTE: BreakpointSource_t and g_breakpoint_source must match!
const char* g_breakpoint_source[NUM_BREAKPOINT_SOURCES] = {
    "A", "X", "Y", "PC", "S", "P",  "C", "Z", "I",
    "D", "B", "R", "V",  "N", "OP", "M", "M", "M"};

// Note: BreakpointOperator_t, _PARAM_BREAKPOINT_, and g_breakpoint_symbols must
// match!
const char* g_breakpoint_symbols[NUM_BREAKPOINT_OPERATORS] = {
    "<=", "< ", "= ", "!=", "> ", ">=", "? ", "@ ", "* "};

auto IsDebugBreakOnInvalid(int iOpcodeType) -> bool {
  extern int g_debug_break_on_invalid;
  g_debug_breakpoint_hit |=
      ((g_debug_break_on_invalid >> iOpcodeType) & 1) ? BP_HIT_INVALID : 0;
  return g_debug_breakpoint_hit != 0;
}

void ClearTempBreakpoints() {
  int iBP = 0;
  while (iBP < MAX_BREAKPOINTS) {
    if (g_breakpoints[iBP].bSet && g_breakpoints[iBP].bTemp) {
      _BWZ_Clear(g_breakpoints, iBP);
      g_breakpoints_count--;
    }
    iBP++;
  }
}

// BWZ (Breakpoint, Watch, ZeroPage) shared helpers
// _______________________________________________

void _BWZ_Clear(Breakpoint_t* aBreakWatchZero, int iSlot) {
  if (aBreakWatchZero) {
    aBreakWatchZero[iSlot].bSet = false;
    aBreakWatchZero[iSlot].bEnabled = false;
    aBreakWatchZero[iSlot].bTemp = false;
    aBreakWatchZero[iSlot].address = 0;
    aBreakWatchZero[iSlot].nLength = 0;
    aBreakWatchZero[iSlot].eSource = static_cast<BreakpointSource_t>(0);
    aBreakWatchZero[iSlot].eOperator = static_cast<BreakpointOperator_t>(0);
  }
}

void _BWZ_RemoveOne(Breakpoint_t* aBreakWatchZero, const int iSlot,
                    int& total) {
  if (aBreakWatchZero) {
    if (aBreakWatchZero[iSlot].bSet) {
      _BWZ_Clear(aBreakWatchZero, iSlot);
      total--;
    }
  }
}

void _BWZ_RemoveAll(Breakpoint_t* aBreakWatchZero, const int nMax, int& total) {
  if (aBreakWatchZero) {
    int i = 0;
    while (i < nMax) {
      _BWZ_Clear(aBreakWatchZero, i);
      i++;
    }
    total = 0;
  }
}

void _BWZ_ClearViaArgs(int nArgs, Breakpoint_t* aBreakWatchZero, const int nMax,
                       int& total) {
  if (aBreakWatchZero) {
    for (int iArg = 1; iArg <= nArgs; iArg++) {
      int iSlot = g_args[iArg].nValue;
      if (iSlot < nMax) {
        _BWZ_RemoveOne(aBreakWatchZero, iSlot, total);
      }
    }
  }
}

void _BWZ_EnableDisableViaArgs(int nArgs, Breakpoint_t* aBreakWatchZero,
                               const int nMax, const bool bEnabled) {
  if (aBreakWatchZero) {
    for (int iArg = 1; iArg <= nArgs; iArg++) {
      int iSlot = g_args[iArg].nValue;
      if (iSlot < nMax) {
        aBreakWatchZero[iSlot].bEnabled = bEnabled;
      }
    }
  }
}

void _BWZ_List(const Breakpoint_t* aBreakWatchZero, const int iBWZ) {
  if (aBreakWatchZero) {
    char sText[CONSOLE_WIDTH];
    const Breakpoint_t* pBWZ = &aBreakWatchZero[iBWZ];

    const char* src_ptr = g_breakpoint_source[pBWZ->eSource];
    const char* pCmp = g_breakpoint_symbols[pBWZ->eOperator];

    sprintf(sText, "  %x: %s %s %04X", iBWZ, src_ptr, pCmp, pBWZ->address);
    if (pBWZ->nLength > 1) {
      char sLen[32];
      sprintf(sLen, ",%04X", pBWZ->nLength);
      strcat(sText, sLen);
    }

    if (!pBWZ->bEnabled) {
      strcat(sText, " (Disabled)");
    }

    ConsoleBufferPush(sText);
  }
}

void _BWZ_ListAll(const Breakpoint_t* aBreakWatchZero, const int nMax) {
  if (aBreakWatchZero) {
    int i = 0;
    while (i < nMax) {
      if (aBreakWatchZero[i].bSet) {
        _BWZ_List(aBreakWatchZero, i);
      }
      i++;
    }
  }
}

// Breakpoints
// ____________________________________________________________________________________

auto CmdBreakpoint(int nArgs) -> Update_t {
  return CmdBreakpointAddSmart(nArgs);
}

auto CmdBreakpointAddSmart(int nArgs) -> Update_t {
  if (!nArgs) {
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
  if (FindParam(g_args[1].sArg, MATCH_EXACT, iSrc, _PARAM_BREAKPOINT_BEGIN,
                _PARAM_BREAKPOINT_END) > 0) {
    return CmdBreakpointAddReg(nArgs);
  }

  return CmdBreakpointAddPC(nArgs);
}

auto CmdBreakpointAddPC(int nArgs) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(CMD_BREAKPOINT_ADD_PC);
  }

  for (int iArg = 1; iArg <= nArgs; iArg++) {
    uint16_t address = g_args[iArg].nValue;
    _CmdBreakpointAddCommonArg(iArg, nArgs, BP_SRC_REG_PC, BP_OP_EQUAL);
    (void)address;
  }

  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddReg(int nArgs) -> Update_t {
  if (nArgs < 3) {
    return Help_Arg_1(CMD_BREAKPOINT_ADD_REG);
  }

  _CmdBreakpointAddCommonArg(1, nArgs, BP_SRC_REG_A, BP_OP_EQUAL);

  return UPDATE_BREAKPOINTS;
}

int _CmdBreakpointAddCommonArg(int iArg, int nArg, BreakpointSource_t iSrc,
                               BreakpointOperator_t iCmp,
                               bool bIsTempBreakpoint) {
  (void)nArg;
  int iBP = 0;
  while ((iBP < MAX_BREAKPOINTS) && (g_breakpoints[iBP].bSet)) {
    iBP++;
  }

  if (iBP >= MAX_BREAKPOINTS) {
    console_display_error("All breakpoints are currently in use.");
    return 0;
  }

  Breakpoint_t* pBP = &g_breakpoints[iBP];
  pBP->bSet = true;
  pBP->bEnabled = true;
  pBP->bTemp = bIsTempBreakpoint;
  pBP->eSource = iSrc;
  pBP->eOperator = iCmp;
  pBP->address = g_args[iArg].nValue;
  pBP->nLength = 1;

  g_breakpoints_count++;

  return 1;
}

auto CmdBreakpointAddIO(int nArgs) -> Update_t {
  if (nArgs < 1) return Help_Arg_1(CMD_BREAKPOINT_ADD_IO);
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddMemA(int nArgs) -> Update_t {
  if (nArgs < 1) return Help_Arg_1(CMD_BREAKPOINT_ADD_MEM);
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddMemR(int nArgs) -> Update_t {
  if (nArgs < 1) return Help_Arg_1(CMD_BREAKPOINT_ADD_MEMR);
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddMemW(int nArgs) -> Update_t {
  if (nArgs < 1) return Help_Arg_1(CMD_BREAKPOINT_ADD_MEMW);
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointEdit(int nArgs) -> Update_t {
  if (nArgs < 1) return Help_Arg_1(CMD_BREAKPOINT_EDIT);
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointClear(int nArgs) -> Update_t {
  if (!g_breakpoints_count) {
    return console_display_error("There are no breakpoints defined.");
  }

  if (!nArgs) {
    _BWZ_RemoveAll(g_breakpoints, MAX_BREAKPOINTS, g_breakpoints_count);
  } else {
    _BWZ_ClearViaArgs(nArgs, g_breakpoints, MAX_BREAKPOINTS,
                      g_breakpoints_count);
  }

  return UPDATE_DISASM | UPDATE_BREAKPOINTS | UPDATE_CONSOLE_DISPLAY;
}

auto CmdBreakpointDisable(int nArgs) -> Update_t {
  if (!g_breakpoints_count) {
    return console_display_error("There are no breakpoints defined.");
  }

  if (!nArgs) {
    return Help_Arg_1(CMD_BREAKPOINT_DISABLE);
  }

  _BWZ_EnableDisableViaArgs(nArgs, g_breakpoints, MAX_BREAKPOINTS, false);

  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointEnable(int nArgs) -> Update_t {
  if (!g_breakpoints_count) {
    return console_display_error("There are no breakpoints defined.");
  }

  if (!nArgs) {
    return Help_Arg_1(CMD_BREAKPOINT_ENABLE);
  }

  _BWZ_EnableDisableViaArgs(nArgs, g_breakpoints, MAX_BREAKPOINTS, true);

  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointList(int nArgs) -> Update_t {
  (void)nArgs;
  if (!g_breakpoints_count) {
    char sText[CONSOLE_WIDTH];
    sprintf(sText, "  There are no current breakpoints.  (Max: %d)",
            MAX_BREAKPOINTS);
    ConsoleBufferPush(sText);
  } else {
    _BWZ_ListAll(g_breakpoints, MAX_BREAKPOINTS);
  }
  return ConsoleUpdate();
}

auto CmdBreakpointSave(int nArgs) -> Update_t {
  char sText[CONSOLE_WIDTH];

  // ConfigSave_PrepareHeader( PARAM_CAT_BREAKPOINTS, CMD_BREAKPOINT_CLEAR );

  int iBreakpoint = 0;
  while (iBreakpoint < MAX_BREAKPOINTS) {
    if (g_breakpoints[iBreakpoint].bSet) {
      sprintf(sText, "%s %x %04X,%04X\n",
              g_commands[CMD_BREAKPOINT_ADD_REG].name, iBreakpoint,
              g_breakpoints[iBreakpoint].address,
              g_breakpoints[iBreakpoint].nLength);
      g_config_state.PushLine(sText);
    }
    if (!g_breakpoints[iBreakpoint].bEnabled) {
      sprintf(sText, "%s %x\n", g_commands[CMD_BREAKPOINT_DISABLE].name,
              iBreakpoint);
      g_config_state.PushLine(sText);
    }

    iBreakpoint++;
  }

  if (nArgs) {
    if (!(g_args[1].bType & TYPE_QUOTED_2)) {
      return Help_Arg_1(CMD_BREAKPOINT_SAVE);
    }

    // if (ConfigSave_BufferToDisk( g_args[ 1 ].sArg, CONFIG_SAVE_FILE_CREATE ))
    {
      ConsoleBufferPush("Saved.");
      return ConsoleUpdate();
    }
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdWatch(int nArgs) -> Update_t { return CmdWatchAdd(nArgs); }

auto CmdWatchAdd(int nArgs) -> Update_t {
  if (!nArgs) {
    return CmdWatchList(0);
  }

  int iArg = 1;
  int iWatch = NO_6502_TARGET;
  if (nArgs > 1) {
    iWatch = static_cast<int>(g_args[1].nValue);
    iArg++;
  }

  bool bAdded = false;
  for (; iArg <= nArgs; iArg++) {
    uint16_t address = g_args[iArg].nValue;

    if ((address >= _6502_IO_BEGIN) && (address <= _6502_IO_END)) {
      return console_display_error("You may not watch an I/O location.");
    }

    if (iWatch == NO_6502_TARGET) {
      iWatch = 0;
      while ((iWatch < MAX_WATCHES) && (g_watches[iWatch].bSet)) {
        iWatch++;
      }
    }

    if ((iWatch >= MAX_WATCHES) && !bAdded) {
      char sText[CONSOLE_WIDTH];
      sprintf(sText, "All watches are currently in use.  (Max: %d)",
              MAX_WATCHES);
      ConsoleDisplayPush(sText);
      return ConsoleUpdate();
    }

    if ((iWatch < MAX_WATCHES) && (g_watches_count < MAX_WATCHES)) {
      g_watches[iWatch].bSet = true;
      g_watches[iWatch].bEnabled = true;
      g_watches[iWatch].address = address;
      bAdded = true;
      g_watches_count++;
      iWatch++;
    }
  }

  if (!bAdded) {
    return Help_Arg_1(CMD_WATCH_ADD);
  }

  return UPDATE_WATCH;
}

auto CmdWatchSave(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdWatchClear(int nArgs) -> Update_t {
  if (!g_watches_count) {
    return console_display_error("There are no watches defined.");
  }

  if (!nArgs) {
    _BWZ_RemoveAll((Breakpoint_t*)g_watches, MAX_WATCHES, g_watches_count);
  } else {
    _BWZ_ClearViaArgs(nArgs, (Breakpoint_t*)g_watches, MAX_WATCHES,
                      g_watches_count);
  }

  return UPDATE_WATCH | UPDATE_CONSOLE_DISPLAY;
}

auto CmdWatchDisable(int nArgs) -> Update_t {
  if (!g_watches_count) {
    return console_display_error("There are no watches defined.");
  }

  if (!nArgs) {
    return Help_Arg_1(CMD_WATCH_DISABLE);
  }

  _BWZ_EnableDisableViaArgs(nArgs, (Breakpoint_t*)g_watches, MAX_WATCHES,
                            false);

  return UPDATE_WATCH;
}

auto CmdWatchEnable(int nArgs) -> Update_t {
  if (!g_watches_count) {
    return console_display_error("There are no watches defined.");
  }

  if (!nArgs) {
    return Help_Arg_1(CMD_WATCH_ENABLE);
  }

  _BWZ_EnableDisableViaArgs(nArgs, (Breakpoint_t*)g_watches, MAX_WATCHES, true);

  return UPDATE_WATCH;
}

auto CmdWatchList(int nArgs) -> Update_t {
  (void)nArgs;
  if (!g_watches_count) {
    char sText[CONSOLE_WIDTH];
    sprintf(sText, "  There are no current watches.  (Max: %d)", MAX_WATCHES);
    ConsoleBufferPush(sText);
  } else {
    _BWZ_ListAll((Breakpoint_t*)g_watches, MAX_WATCHES);
  }
  return ConsoleUpdate();
}
