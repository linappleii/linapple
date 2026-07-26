#include "Debugger_Cmd_Window.h"

#include <cassert>
#include <cstddef>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Color.h"
#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Help.h"
#include "Debugger_Memory.h"
#include "Debugger_Parser.h"
#include "Video.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

// Globals originally from Debug.cpp
extern int g_iWindowLast;
extern int g_iWindowThis;
extern WindowSplit_t g_aWindowConfig[NUM_WINDOWS];

extern int g_nConsoleDisplayLines;
extern bool g_bConsoleFullWidth;
extern int g_nConsoleDisplayWidth;
extern int g_nDisasmWinHeight;
extern int g_nDisasmCurLine;

extern uint16_t g_nDisasmTopAddress;
extern uint16_t g_nDisasmBotAddress;
extern uint16_t g_nDisasmCurAddress;
extern bool g_bDisasmCurBad;

extern uint32_t g_uVideoMode;

const int MIN_DISPLAY_CONSOLE_LINES = 5;

// Implementation

//===========================================================================
void _WindowJoin() { g_aWindowConfig[g_iWindowThis].bSplit = false; }

//===========================================================================
void _WindowSplit(Window_e eNewBottomWindow) {
  g_aWindowConfig[g_iWindowThis].bSplit = true;
  g_aWindowConfig[g_iWindowThis].eBot = eNewBottomWindow;
}

//===========================================================================
void _WindowLast() {
  int eNew = g_iWindowLast;
  g_iWindowLast = g_iWindowThis;
  g_iWindowThis = eNew;
}

//===========================================================================
void _WindowSwitch(int eNewWindow) {
  g_iWindowLast = g_iWindowThis;
  g_iWindowThis = eNewWindow;
}

//===========================================================================
auto CmdWindowViewCommon(int iNewWindow) -> Update_t {
  // Switching to same window, remove split
  if (g_iWindowThis == iNewWindow) {
    g_aWindowConfig[iNewWindow].bSplit = false;
  } else {
    _WindowSwitch(iNewWindow);
  }

  WindowUpdateSizes();
  return UPDATE_ALL;
}

//===========================================================================
auto _CmdWindowViewFull(int iNewWindow) -> Update_t {
  if (g_iWindowThis != iNewWindow) {
    g_aWindowConfig[iNewWindow].bSplit = false;
    _WindowSwitch(iNewWindow);
    WindowUpdateConsoleDisplayedSize();
  }
  return UPDATE_ALL;
}

//===========================================================================
void WindowUpdateConsoleDisplayedSize() {
  g_nConsoleDisplayLines = MIN_DISPLAY_CONSOLE_LINES;
#if USE_APPLE_FONT
  g_bConsoleFullWidth = true;
  g_nConsoleDisplayWidth = CONSOLE_WIDTH - 1;

  if (g_iWindowThis == WINDOW_CONSOLE) {
    g_nConsoleDisplayLines = MAX_DISPLAY_LINES;
    g_nConsoleDisplayWidth = CONSOLE_WIDTH - 1;
    g_bConsoleFullWidth = true;
  }
#else
  g_nConsoleDisplayWidth = (CONSOLE_WIDTH / 2) + 10;
  g_bConsoleFullWidth = false;

  if (g_iWindowThis == WINDOW_CONSOLE) {
    g_nConsoleDisplayLines = MAX_DISPLAY_LINES;
    g_nConsoleDisplayWidth = CONSOLE_WIDTH - 1;
    g_bConsoleFullWidth = true;
  }
#endif
}

//===========================================================================
auto WindowGetHeight(int iWindow) -> int {
  (void)iWindow;
  return g_nDisasmWinHeight;
}

//===========================================================================
void WindowUpdateDisasmSize() {
  if (g_aWindowConfig[g_iWindowThis].bSplit) {
    g_nDisasmWinHeight = (MAX_DISPLAY_LINES - g_nConsoleDisplayLines) / 2;
  } else {
    g_nDisasmWinHeight = MAX_DISPLAY_LINES - g_nConsoleDisplayLines;
  }
  g_nDisasmCurLine = MAX(0, (g_nDisasmWinHeight - 1) / 2);
}

//===========================================================================
void WindowUpdateSizes() {
  WindowUpdateDisasmSize();
  WindowUpdateConsoleDisplayedSize();
}

//===========================================================================
auto CmdWindowCycleNext(int nArgs) -> Update_t {
  (void)nArgs;
  g_iWindowThis++;
  if (g_iWindowThis >= NUM_WINDOWS) {
    g_iWindowThis = 0;
  }

  WindowUpdateSizes();

  return UPDATE_ALL;
}

//===========================================================================
auto CmdWindowCyclePrev(int nArgs) -> Update_t {
  (void)nArgs;
  g_iWindowThis--;
  if (g_iWindowThis < 0) {
    g_iWindowThis = NUM_WINDOWS - 1;
  }

  WindowUpdateSizes();

  return UPDATE_ALL;
}

//===========================================================================
auto CmdWindowShowCode(int nArgs) -> Update_t {
  (void)nArgs;

  if (g_iWindowThis == WINDOW_CODE) {
    g_aWindowConfig[g_iWindowThis].bSplit = false;
    g_aWindowConfig[g_iWindowThis].eBot =
        WINDOW_CODE;  // not really needed, but SAFE HEX ;-)
  } else if (g_iWindowThis == WINDOW_DATA) {
    g_aWindowConfig[g_iWindowThis].bSplit = true;
    g_aWindowConfig[g_iWindowThis].eBot = WINDOW_CODE;
  }

  WindowUpdateSizes();

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowCode1(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowCode2(int nArgs) -> Update_t {
  (void)nArgs;
  if ((g_iWindowThis == WINDOW_CODE) || (g_iWindowThis == WINDOW_DATA)) {
    if (g_iWindowThis == WINDOW_CODE) {
      _WindowJoin();
      WindowUpdateDisasmSize();
    } else if (g_iWindowThis == WINDOW_DATA) {
      _WindowSplit(WINDOW_CODE);
      WindowUpdateDisasmSize();
    }
    return UPDATE_DISASM;
  }
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowData(int nArgs) -> Update_t {
  (void)nArgs;
  if (g_iWindowThis == WINDOW_CODE) {
    g_aWindowConfig[g_iWindowThis].bSplit = true;
    g_aWindowConfig[g_iWindowThis].eBot = WINDOW_DATA;
    return UPDATE_ALL;
  } else if (g_iWindowThis == WINDOW_DATA) {
    g_aWindowConfig[g_iWindowThis].bSplit = false;
    g_aWindowConfig[g_iWindowThis].eBot =
        WINDOW_DATA;  // not really needed, but SAFE HEX ;-)
    return UPDATE_ALL;
  }

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowData1(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowData2(int nArgs) -> Update_t {
  (void)nArgs;
  if ((g_iWindowThis == WINDOW_CODE) || (g_iWindowThis == WINDOW_DATA)) {
    if (g_iWindowThis == WINDOW_CODE) {
      _WindowSplit(WINDOW_DATA);
    } else if (g_iWindowThis == WINDOW_DATA) {
      _WindowJoin();
    }
    return UPDATE_DISASM;
  }
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowSource(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowSource1(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowSource2(int nArgs) -> Update_t {
  (void)nArgs;
  _WindowSplit(WINDOW_SOURCE);
  WindowUpdateSizes();

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowViewCode(int nArgs) -> Update_t {
  (void)nArgs;
  return CmdWindowViewCommon(WINDOW_CODE);
}

//===========================================================================
auto CmdWindowViewConsole(int nArgs) -> Update_t {
  (void)nArgs;
  return _CmdWindowViewFull(WINDOW_CONSOLE);
}

//===========================================================================
auto CmdWindowViewData(int nArgs) -> Update_t {
  (void)nArgs;
  return CmdWindowViewCommon(WINDOW_DATA);
}

//===========================================================================
auto CmdWindowViewOutput(int nArgs) -> Update_t {
  (void)nArgs;
  video_redraw_screen();

  DebugVideoMode::Instance().Set(g_uVideoMode);

  return UPDATE_NOTHING;  // intentional
}

//===========================================================================
auto CmdWindowViewSource(int nArgs) -> Update_t {
  (void)nArgs;
  return _CmdWindowViewFull(WINDOW_CONSOLE);
}

//===========================================================================
auto CmdWindowViewSymbols(int nArgs) -> Update_t {
  (void)nArgs;
  return _CmdWindowViewFull(WINDOW_CONSOLE);
}

//===========================================================================
auto CmdWindow(int nArgs) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(CMD_WINDOW);
  }

  int iParam = 0;
  char* pName = g_aArgs[1].sArg;
  int nFound = FindParam(pName, MATCH_EXACT, iParam, _PARAM_WINDOW_BEGIN,
                         _PARAM_WINDOW_END);
  if (nFound) {
    switch (iParam) {
      case PARAM_CODE:
        return CmdWindowViewCode(0);
        break;
      case PARAM_CONSOLE:
        return CmdWindowViewConsole(0);
        break;
      case PARAM_DATA:
        return CmdWindowViewData(0);
        break;
      case PARAM_SOURCE:
        return CmdWindowViewSource(0);
        break;
      case PARAM_SYMBOLS:
        return CmdWindowViewSymbols(0);
        break;
      default:
        return Help_Arg_1(CMD_WINDOW);
        break;
    }
  }

  WindowUpdateConsoleDisplayedSize();

  return UPDATE_ALL;
}

//===========================================================================
auto CmdWindowLast(int nArgs) -> Update_t {
  (void)nArgs;
  _WindowLast();
  WindowUpdateConsoleDisplayedSize();
  return UPDATE_ALL;
}

void _CursorMoveDownAligned(int nDelta) {
  if (g_iWindowThis == WINDOW_DATA) {
    g_nDisasmCurAddress = static_cast<uint16_t>(g_nDisasmCurAddress + nDelta);
    g_aMemDump[0].nAddress = g_nDisasmCurAddress;
  } else {
    g_nDisasmCurAddress =
        DisasmCalcAddressFromLines(g_nDisasmCurAddress, nDelta);
    DisasmCalcTopFromCurAddress(true);
  }
}

void _CursorMoveUpAligned(int nDelta) {
  if (g_iWindowThis == WINDOW_DATA) {
    g_nDisasmCurAddress = static_cast<uint16_t>(g_nDisasmCurAddress - nDelta);
    g_aMemDump[0].nAddress = g_nDisasmCurAddress;
  } else {
    g_nDisasmTopAddress = static_cast<uint16_t>(g_nDisasmTopAddress - nDelta);
    DisasmCalcCurFromTopAddress();
    DisasmCalcBotFromTopAddress();
  }
}

//===========================================================================
void DisasmCalcTopFromCurAddress(bool bUpdateTop) {
  (void)bUpdateTop;
  int nLen = ((g_nDisasmWinHeight - g_nDisasmCurLine) *
              3);  // max 3 opcodes/instruction, is our search window

  // Look for a start address that when disassembled,
  // will have the cursor on the specified line and address
  int iTop = g_nDisasmCurAddress - nLen;
  int iCur = g_nDisasmCurAddress;

  g_bDisasmCurBad = false;

  bool bFound = false;
  while (iTop <= iCur) {
    auto iAddress = static_cast<uint16_t>(iTop);
    int iOpmode = 0;
    int nOpbytes = 0;

    for (int iLine = 0; iLine <= g_nDisasmCurLine; iLine++)
    {
      _6502_GetOpmodeOpbyte(iAddress, iOpmode, nOpbytes);

      if (iLine == g_nDisasmCurLine) {
        if (iAddress == g_nDisasmCurAddress) {
          g_nDisasmTopAddress = static_cast<uint16_t>(iTop);
          bFound = true;
          break;
        }
      }
      if (iAddress >= g_nDisasmCurAddress) {
        break;
      }
      iAddress += nOpbytes;
    }
    if (bFound) {
      break;
    }
    iTop++;
  }

  if (!bFound) {
    g_nDisasmTopAddress = g_nDisasmCurAddress;
    g_bDisasmCurBad = true;
  }
}

//===========================================================================
auto DisasmCalcAddressFromLines(uint16_t iAddress, int nLines) -> uint16_t {
  while (nLines-- > 0) {
    int iOpmode = 0;
    int nOpbytes = 0;
    _6502_GetOpmodeOpbyte(iAddress, iOpmode, nOpbytes);
    iAddress += nOpbytes;
  }
  return iAddress;
}

//===========================================================================
void DisasmCalcCurFromTopAddress() {
  g_nDisasmCurAddress =
      DisasmCalcAddressFromLines(g_nDisasmTopAddress, g_nDisasmCurLine);
}

//===========================================================================
void DisasmCalcBotFromTopAddress() {
  g_nDisasmBotAddress =
      DisasmCalcAddressFromLines(g_nDisasmTopAddress, g_nDisasmWinHeight);
}

//===========================================================================
void DisasmCalcTopBotAddress() {
  DisasmCalcTopFromCurAddress();
  DisasmCalcBotFromTopAddress();
}

auto debug_get_video_mode(uint32_t* pVideoMode) -> bool {
  return DebugVideoMode::Instance().Get(pVideoMode);
}
auto CmdCursorFollowTarget(int nArgs) -> Update_t {
  uint16_t nAddress = 0;
  if (_6502_GetTargetAddress(g_nDisasmCurAddress, nAddress)) {
    g_nDisasmCurAddress = nAddress;

    if (CURSOR_ALIGN_CENTER == nArgs) {
      WindowUpdateDisasmSize();
    } else if (CURSOR_ALIGN_TOP == nArgs) {
      g_nDisasmCurLine = 0;
    }
    DisasmCalcTopBotAddress();
  }

  return UPDATE_ALL;
}

auto CmdCursorLineUp(int nArgs) -> Update_t {
  if (g_iWindowThis == WINDOW_DATA) {
    _CursorMoveUpAligned(WINDOW_DATA_BYTES_PER_LINE);
    DisasmCalcTopBotAddress();
  } else if (nArgs) {
    g_nDisasmTopAddress--;
    DisasmCalcCurFromTopAddress();
    DisasmCalcBotFromTopAddress();
  } else {
    g_nDisasmTopAddress--;
    DisasmCalcCurFromTopAddress();
    DisasmCalcBotFromTopAddress();
  }
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorLineDown(int nArgs) -> Update_t {
  int iOpmode = 0;
  int nOpbytes = 0;
  _6502_GetOpmodeOpbyte(g_nDisasmCurAddress, iOpmode,
                        nOpbytes);  // g_nDisasmTopAddress

  if (g_iWindowThis == WINDOW_DATA) {
    _CursorMoveDownAligned(WINDOW_DATA_BYTES_PER_LINE);
    DisasmCalcTopBotAddress();
  } else if (nArgs)  // scroll down by 'n' bytes
  {
    nOpbytes = nArgs;  // HACKL g_aArgs[1].val

    g_nDisasmTopAddress += nOpbytes;
    g_nDisasmCurAddress += nOpbytes;
    g_nDisasmBotAddress += nOpbytes;
    DisasmCalcTopBotAddress();
  } else {
#if DEBUG_SCROLL == 6
    // Works except on one case: G FB53, SPACE, DOWN
    uint16_t nTop = g_nDisasmTopAddress;
    uint16_t nCur = g_nDisasmCurAddress + nOpbytes;
    if (g_bDisasmCurBad) {
      g_nDisasmCurAddress = nCur;
      g_bDisasmCurBad = false;
      DisasmCalcTopFromCurAddress();
      return UPDATE_DISASM;
    }

    // Adjust Top until nNewCur is at > Cur
    do {
      g_nDisasmTopAddress++;
      DisasmCalcCurFromTopAddress();
    } while (g_nDisasmCurAddress < nCur);

    DisasmCalcCurFromTopAddress();
    DisasmCalcBotFromTopAddress();
    g_bDisasmCurBad = false;
#endif
    g_nDisasmCurAddress += nOpbytes;

    _6502_GetOpmodeOpbyte(g_nDisasmTopAddress, iOpmode, nOpbytes);
    g_nDisasmTopAddress += nOpbytes;

    _6502_GetOpmodeOpbyte(g_nDisasmBotAddress, iOpmode, nOpbytes);
    g_nDisasmBotAddress += nOpbytes;

    if (g_bDisasmCurBad) {
      //  MessageBox( NULL, "Bad Disassembly of opcodes", "Debugger", MB_OK );

      //      g_nDisasmCurAddress = nCur;
      //      g_bDisasmCurBad = false;
      //      DisasmCalcTopFromCurAddress();
      DisasmCalcTopBotAddress();
      //      return UPDATE_DISASM;
    }
    g_bDisasmCurBad = false;
  }

  // Can't use use + nBytes due to Disasm Singularity
  //  DisasmCalcTopBotAddress();

  return UPDATE_DISASM;
}

// C++ Bug, can't have local structs used in STL containers
struct LookAhead_t {
  int _nAddress;
  int _iOpcode;
  int _iOpmode;
  int _nOpbytes;
};

auto CmdCursorJumpPC(int nArgs) -> Update_t {
  // TODO: Allow user to decide if they want next g_aOpcodes at
  // 1) Centered (traditionaly), or
  // 2) Top of the screen

  // if (UserPrefs.bNextInstructionCentered)
  if (CURSOR_ALIGN_CENTER == nArgs) {
    g_nDisasmCurAddress = cpu_get_registers()->pc;  // (2)
    WindowUpdateDisasmSize();                     // calc cur line
  } else if (CURSOR_ALIGN_TOP == nArgs) {
    g_nDisasmCurAddress = cpu_get_registers()->pc;  // (2)
    g_nDisasmCurLine = 0;
  }

  DisasmCalcTopBotAddress();

  return UPDATE_ALL;
}

//===========================================================================
auto CmdCursorJumpRetAddr(int nArgs) -> Update_t {
  uint16_t nAddress = 0;
  if (_6502_GetStackReturnAddress(nAddress)) {
    g_nDisasmCurAddress = nAddress;

    if (CURSOR_ALIGN_CENTER == nArgs) {
      WindowUpdateDisasmSize();
    } else if (CURSOR_ALIGN_TOP == nArgs) {
      g_nDisasmCurLine = 0;
    }
    DisasmCalcTopBotAddress();
  }

  return UPDATE_ALL;
}

auto CmdCursorPageDown(int nArgs) -> Update_t {
  (void)nArgs;
  int iLines = 0;  // show at least 1 line from previous display
  int nLines = WindowGetHeight(g_iWindowThis);

  if (nLines < 2) {
    nLines = 2;
  }

  if (g_iWindowThis == WINDOW_DATA) {
    const int nStep = 128;
    _CursorMoveDownAligned(nStep);
  } else {
    // 4
    //    while (++iLines < nLines)
    //      CmdCursorLineDown(nArgs);

    // 5
    nLines -= (g_nDisasmCurLine + 1);
    if (nLines < 1) {
      nLines = 1;
    }

    while (iLines++ < nLines) {
      CmdCursorLineDown(0);  // nArgs
    }
    // 6
  }

  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorPageDown256(int nArgs) -> Update_t {
  (void)nArgs;
  const int nStep = 256;
  _CursorMoveDownAligned(nStep);
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorPageDown4K(int nArgs) -> Update_t {
  (void)nArgs;
  const int nStep = 4096;
  _CursorMoveDownAligned(nStep);
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorPageUp(int nArgs) -> Update_t {
  (void)nArgs;
  int iLines = 0;  // show at least 1 line from previous display
  int nLines = WindowGetHeight(g_iWindowThis);

  if (nLines < 2) {
    nLines = 2;
  }

  if (g_iWindowThis == WINDOW_DATA) {
    const int nStep = 128;
    _CursorMoveUpAligned(nStep);
  } else {
    //    while (++iLines < nLines)
    //      CmdCursorLineUp(nArgs);
    nLines -= (g_nDisasmCurLine + 1);
    if (nLines < 1) {
      nLines = 1;
    }

    while (iLines++ < nLines) {
      CmdCursorLineUp(0);  // smart line up
      // CmdCursorLineUp( -nLines );
    }
  }

  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorPageUp256(int nArgs) -> Update_t {
  (void)nArgs;
  const int nStep = 256;
  _CursorMoveUpAligned(nStep);
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorPageUp4K(int nArgs) -> Update_t {
  (void)nArgs;
  const int nStep = 4096;
  _CursorMoveUpAligned(nStep);
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorSetPC(int nArgs) -> Update_t  // TODO rename
{
  (void)nArgs;
  cpu_get_registers()->pc =
      g_nDisasmCurAddress;  // set PC to current cursor address
  return UPDATE_DISASM;
}

// Flags
// __________________________________________________________________________________________

auto CmdViewOutput_Text4X(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_X, VF_TEXT);
}
auto CmdViewOutput_Text41(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_1, VF_TEXT);
}
auto CmdViewOutput_Text42(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_2, VF_TEXT);
}
// Text 80
auto CmdViewOutput_Text8X(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_X, VF_TEXT | VF_80COL);
}
auto CmdViewOutput_Text81(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_1, VF_TEXT | VF_80COL);
}
auto CmdViewOutput_Text82(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_2, VF_TEXT | VF_80COL);
}
// Lo-Res
auto CmdViewOutput_GRX(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_X, 0);
}
auto CmdViewOutput_GR1(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_1, 0);
}
auto CmdViewOutput_GR2(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_2, 0);
}
// Double Lo-Res
auto CmdViewOutput_DGRX(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_X, VF_DHIRES | VF_80COL);
}
auto CmdViewOutput_DGR1(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_1, VF_DHIRES | VF_80COL);
}
auto CmdViewOutput_DGR2(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_2, VF_DHIRES | VF_80COL);
}
// Hi-Res
auto CmdViewOutput_HGRX(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_X, VF_HIRES);
}
auto CmdViewOutput_HGR1(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_1, VF_HIRES);
}
auto CmdViewOutput_HGR2(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_2, VF_HIRES);
}
// Double Hi-Res
auto CmdViewOutput_DHGRX(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_X, VF_HIRES | VF_DHIRES | VF_80COL);
}
auto CmdViewOutput_DHGR1(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_1, VF_HIRES | VF_DHIRES | VF_80COL);
}
auto CmdViewOutput_DHGR2(int nArgs) -> Update_t {
  (void)nArgs;
  return _ViewOutput(VIEW_PAGE_2, VF_HIRES | VF_DHIRES | VF_80COL);
}

// Watches
// ________________________________________________________________________________________

auto _ViewOutput(ViewVideoPage_t iPage, int bVideoModeFlags) -> Update_t {
  switch (iPage) {
    case VIEW_PAGE_X:
      bVideoModeFlags |= !video_get_sw_page2() ? 0 : VF_PAGE2;
      bVideoModeFlags |= !video_get_sw_mixed() ? 0 : VF_MIXED;
      break;  // Page Current & current MIXED state
    case VIEW_PAGE_1:
      bVideoModeFlags |= 0;
      break;  // Page 1
    case VIEW_PAGE_2:
      bVideoModeFlags |= VF_PAGE2;
      break;  // Page 2
    default:
      assert(0);
      break;
  }

  DebugVideoMode::Instance().Set(bVideoModeFlags);
  video_refresh_screen(bVideoModeFlags, true);
  return UPDATE_NOTHING;  // intentional
}
