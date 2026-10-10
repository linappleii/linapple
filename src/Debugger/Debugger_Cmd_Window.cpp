// SPDX-License-Identifier: GPL-2.0-only
#include "Debugger_Cmd_Window.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Memory.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"
#include "apple2/CPU.h"
#include "apple2/Video.h"

// Globals originally from Debug.cpp

const int MIN_DISPLAY_CONSOLE_LINES = 5;

// Implementation

//===========================================================================
auto WindowJoin() -> void { window_config[window_this].bSplit = false; }

//===========================================================================
auto WindowSplit(Window eNewBottomWindow) -> void {
  window_config[window_this].bSplit = true;
  window_config[window_this].eBot = eNewBottomWindow;
}

//===========================================================================
auto WindowLast() -> void {
  int eNew = window_last;
  window_last = window_this;
  window_this = eNew;
}

//===========================================================================
auto WindowSwitch(int eNewWindow) -> void {
  window_last = window_this;
  window_this = eNewWindow;
}

//===========================================================================
auto CmdWindowViewCommon(int iNewWindow) -> UpdateResult {
  // Switching to same window, remove split
  if (window_this == iNewWindow) {
    window_config[iNewWindow].bSplit = false;
  } else {
    WindowSwitch(iNewWindow);
  }

  WindowUpdateSizes();
  return UPDATE_ALL;
}

//===========================================================================
auto CmdWindowViewFull(int iNewWindow) -> UpdateResult {
  if (window_this != iNewWindow) {
    window_config[iNewWindow].bSplit = false;
    WindowSwitch(iNewWindow);
    WindowUpdateConsoleDisplayedSize();
  }
  return UPDATE_ALL;
}

//===========================================================================
auto WindowUpdateConsoleDisplayedSize() -> void {
  console_display_lines = MIN_DISPLAY_CONSOLE_LINES;
  console_full_width = true;
  console_display_width = CONSOLE_WIDTH - 1;

  if (window_this == WINDOW_CONSOLE) {
    console_display_lines = MAX_DISPLAY_LINES;
    console_display_width = CONSOLE_WIDTH - 1;
    console_full_width = true;
  }
}

//===========================================================================
auto WindowGetHeight(int iWindow) -> int {
  (void)iWindow;
  return disasm_win_height;
}

//===========================================================================
auto WindowUpdateDisasmSize() -> void {
  if (window_config[window_this].bSplit) {
    disasm_win_height = (MAX_DISPLAY_LINES - console_display_lines) / 2;
  } else {
    disasm_win_height = MAX_DISPLAY_LINES - console_display_lines;
  }
  disasm_cur_line = std::max(0, (disasm_win_height - 1) / 2);
}

//===========================================================================
auto WindowUpdateSizes() -> void {
  WindowUpdateDisasmSize();
  WindowUpdateConsoleDisplayedSize();
}

//===========================================================================
auto CmdWindowCycleNext(int nArgs) -> UpdateResult {
  (void)nArgs;
  window_this++;
  if (window_this >= NUM_WINDOWS) {
    window_this = 0;
  }

  WindowUpdateSizes();

  return UPDATE_ALL;
}

//===========================================================================
auto CmdWindowCyclePrev(int nArgs) -> UpdateResult {
  (void)nArgs;
  window_this--;
  if (window_this < 0) {
    window_this = NUM_WINDOWS - 1;
  }

  WindowUpdateSizes();

  return UPDATE_ALL;
}

//===========================================================================
auto CmdWindowShowCode(int nArgs) -> UpdateResult {
  (void)nArgs;

  if (window_this == WINDOW_CODE) {
    window_config[window_this].bSplit = false;
    window_config[window_this].eBot =
        WINDOW_CODE;  // not really needed, but SAFE HEX ;-)
  } else if (window_this == WINDOW_DATA) {
    window_config[window_this].bSplit = true;
    window_config[window_this].eBot = WINDOW_CODE;
  }

  WindowUpdateSizes();

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowCode1(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowCode2(int nArgs) -> UpdateResult {
  (void)nArgs;
  if ((window_this == WINDOW_CODE) || (window_this == WINDOW_DATA)) {
    if (window_this == WINDOW_CODE) {
      WindowJoin();
      WindowUpdateDisasmSize();
    } else if (window_this == WINDOW_DATA) {
      WindowSplit(WINDOW_CODE);
      WindowUpdateDisasmSize();
    }
    return UPDATE_DISASM;
  }
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowData(int nArgs) -> UpdateResult {
  (void)nArgs;
  if (window_this == WINDOW_CODE) {
    window_config[window_this].bSplit = true;
    window_config[window_this].eBot = WINDOW_DATA;
    return UPDATE_ALL;
  }
  if (window_this == WINDOW_DATA) {
    window_config[window_this].bSplit = false;
    window_config[window_this].eBot =
        WINDOW_DATA;  // not really needed, but SAFE HEX ;-)
    return UPDATE_ALL;
  }

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowData1(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowData2(int nArgs) -> UpdateResult {
  (void)nArgs;
  if ((window_this == WINDOW_CODE) || (window_this == WINDOW_DATA)) {
    if (window_this == WINDOW_CODE) {
      WindowSplit(WINDOW_DATA);
    } else if (window_this == WINDOW_DATA) {
      WindowJoin();
    }
    return UPDATE_DISASM;
  }
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowSource(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowSource1(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowShowSource2(int nArgs) -> UpdateResult {
  (void)nArgs;
  WindowSplit(WINDOW_SOURCE);
  WindowUpdateSizes();

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdWindowViewCode(int nArgs) -> UpdateResult {
  (void)nArgs;
  return CmdWindowViewCommon(WINDOW_CODE);
}

//===========================================================================
auto CmdWindowViewConsole(int nArgs) -> UpdateResult {
  (void)nArgs;
  return CmdWindowViewFull(WINDOW_CONSOLE);
}

//===========================================================================
auto CmdWindowViewData(int nArgs) -> UpdateResult {
  (void)nArgs;
  return CmdWindowViewCommon(WINDOW_DATA);
}

//===========================================================================
auto CmdWindowViewOutput(int nArgs) -> UpdateResult {
  (void)nArgs;
  video_redraw_screen();

  DebugVideoMode::Instance().Set(video_mode);

  return UPDATE_NOTHING;  // intentional
}

//===========================================================================
auto CmdWindowViewSource(int nArgs) -> UpdateResult {
  (void)nArgs;
  return CmdWindowViewFull(WINDOW_CONSOLE);
}

//===========================================================================
auto CmdWindowViewSymbols(int nArgs) -> UpdateResult {
  (void)nArgs;
  return CmdWindowViewFull(WINDOW_CONSOLE);
}

//===========================================================================
auto CmdWindow(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    return Help_Arg_1(CMD_WINDOW);
  }

  int iParam = 0;
  char* pName = args[1].sArg;
  int nFound = FindParam(pName, MATCH_EXACT, iParam, PARAM_WINDOW_BEGIN,
                         PARAM_WINDOW_END);
  if (nFound != 0) {
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
auto CmdWindowLast(int nArgs) -> UpdateResult {
  (void)nArgs;
  WindowLast();
  WindowUpdateConsoleDisplayedSize();
  return UPDATE_ALL;
}

auto CursorMoveDownAligned(int nDelta) -> void {
  if (window_this == WINDOW_DATA) {
    disasm_cur_address = static_cast<uint16_t>(disasm_cur_address + nDelta);
    mem_dump[0].address = disasm_cur_address;
  } else {
    disasm_cur_address =
        DisasmCalcAddressFromLines(disasm_cur_address, nDelta);
    DisasmCalcTopFromCurAddress(true);
  }
}

auto CursorMoveUpAligned(int nDelta) -> void {
  if (window_this == WINDOW_DATA) {
    disasm_cur_address = static_cast<uint16_t>(disasm_cur_address - nDelta);
    mem_dump[0].address = disasm_cur_address;
  } else {
    disasm_top_address = static_cast<uint16_t>(disasm_top_address - nDelta);
    DisasmCalcCurFromTopAddress();
    DisasmCalcBotFromTopAddress();
  }
}

//===========================================================================
auto DisasmCalcTopFromCurAddress(bool bUpdateTop) -> void {
  (void)bUpdateTop;
  int nLen = ((disasm_win_height - disasm_cur_line) *
              3);  // max 3 opcodes/instruction, is our search window

  // Look for a start address that when disassembled,
  // will have the cursor on the specified line and address
  int iTop = disasm_cur_address - nLen;
  int iCur = disasm_cur_address;

  disasm_cur_bad = false;

  bool bFound = false;
  while (iTop <= iCur) {
    auto iAddress = static_cast<uint16_t>(iTop);
    int iOpmode = 0;
    int nOpbytes = 0;

    for (int iLine = 0; iLine <= disasm_cur_line; iLine++) {
      GetOpmodeOpbyte(iAddress, iOpmode, nOpbytes);

      if ((iLine == disasm_cur_line) && (iAddress == disasm_cur_address)) {
        disasm_top_address = static_cast<uint16_t>(iTop);
        bFound = true;
        break;
      }

      if (iAddress >= disasm_cur_address) {
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
    disasm_top_address = disasm_cur_address;
    disasm_cur_bad = true;
  }
}

//===========================================================================
auto DisasmCalcAddressFromLines(uint16_t iAddress, int nLines) -> uint16_t {
  while (nLines-- > 0) {
    int iOpmode = 0;
    int nOpbytes = 0;
    GetOpmodeOpbyte(iAddress, iOpmode, nOpbytes);
    iAddress += nOpbytes;
  }
  return iAddress;
}

//===========================================================================
auto DisasmCalcCurFromTopAddress() -> void {
  disasm_cur_address =
      DisasmCalcAddressFromLines(disasm_top_address, disasm_cur_line);
}

//===========================================================================
auto DisasmCalcBotFromTopAddress() -> void {
  disasm_bot_address =
      DisasmCalcAddressFromLines(disasm_top_address, disasm_win_height);
}

//===========================================================================
auto DisasmCalcTopBotAddress() -> void {
  DisasmCalcTopFromCurAddress();
  DisasmCalcBotFromTopAddress();
}

auto debug_get_video_mode(uint32_t* pVideoMode) -> bool {
  return DebugVideoMode::Instance().Get(pVideoMode);
}
auto CmdCursorFollowTarget(int nArgs) -> UpdateResult {
  uint16_t target_address = 0;
  if (GetTargetAddress(disasm_cur_address, target_address)) {
    disasm_cur_address = target_address;

    if (CURSOR_ALIGN_CENTER == nArgs) {
      WindowUpdateDisasmSize();
    } else if (CURSOR_ALIGN_TOP == nArgs) {
      disasm_cur_line = 0;
    }
    DisasmCalcTopBotAddress();
  }

  return UPDATE_ALL;
}

auto CmdCursorLineUp(int /*nArgs*/) -> UpdateResult {
  if (window_this == WINDOW_DATA) {
    CursorMoveUpAligned(WINDOW_DATA_BYTES_PER_LINE);
    DisasmCalcTopBotAddress();
  } else {
    disasm_top_address--;
    DisasmCalcCurFromTopAddress();
    DisasmCalcBotFromTopAddress();
  }
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorLineDown(int nArgs) -> UpdateResult {
  int iOpmode = 0;
  int nOpbytes = 0;
  GetOpmodeOpbyte(disasm_cur_address, iOpmode,
                  nOpbytes);  // disasm_top_address

  if (window_this == WINDOW_DATA) {
    CursorMoveDownAligned(WINDOW_DATA_BYTES_PER_LINE);
    DisasmCalcTopBotAddress();
  } else if (nArgs != 0)  // scroll down by 'n' bytes
  {
    nOpbytes = nArgs;  // HACKL args[1].val

    disasm_top_address += nOpbytes;
    disasm_cur_address += nOpbytes;
    disasm_bot_address += nOpbytes;
    DisasmCalcTopBotAddress();
  } else {
    disasm_cur_address += nOpbytes;

    GetOpmodeOpbyte(disasm_top_address, iOpmode, nOpbytes);
    disasm_top_address += nOpbytes;

    GetOpmodeOpbyte(disasm_bot_address, iOpmode, nOpbytes);
    disasm_bot_address += nOpbytes;

    if (disasm_cur_bad) {
      //  MessageBox( nullptr, "Bad Disassembly of opcodes", "Debugger", MB_OK
      //  );

      //      disasm_cur_address = nCur;
      //      disasm_cur_bad = false;
      //      DisasmCalcTopFromCurAddress();
      DisasmCalcTopBotAddress();
      //      return UPDATE_DISASM;
    }
    disasm_cur_bad = false;
  }

  // Can't use use + nBytes due to Disasm Singularity
  //  DisasmCalcTopBotAddress();

  return UPDATE_DISASM;
}

auto CmdCursorJumpPC(int nArgs) -> UpdateResult {
  // TODO: Allow user to decide if they want next opcodes at
  // 1) Centered (traditionaly), or
  // 2) Top of the screen

  // if (UserPrefs.bNextInstructionCentered)
  if (CURSOR_ALIGN_CENTER == nArgs) {
    disasm_cur_address = cpu_get_registers()->pc;  // (2)
    WindowUpdateDisasmSize();                        // calc cur line
  } else if (CURSOR_ALIGN_TOP == nArgs) {
    disasm_cur_address = cpu_get_registers()->pc;  // (2)
    disasm_cur_line = 0;
  }

  DisasmCalcTopBotAddress();

  return UPDATE_ALL;
}

//===========================================================================
auto CmdCursorJumpRetAddr(int nArgs) -> UpdateResult {
  uint16_t address = 0;
  if (GetStackReturnAddress(address)) {
    disasm_cur_address = address;

    if (CURSOR_ALIGN_CENTER == nArgs) {
      WindowUpdateDisasmSize();
    } else if (CURSOR_ALIGN_TOP == nArgs) {
      disasm_cur_line = 0;
    }
    DisasmCalcTopBotAddress();
  }

  return UPDATE_ALL;
}

auto CmdCursorPageDown(int nArgs) -> UpdateResult {
  (void)nArgs;
  int iLines = 0;  // show at least 1 line from previous display
  int nLines = WindowGetHeight(window_this);

  nLines = std::max(nLines, 2);

  if (window_this == WINDOW_DATA) {
    const int nStep = 128;
    CursorMoveDownAligned(nStep);
  } else {
    // 4
    //    while (++iLines < nLines)
    //      CmdCursorLineDown(nArgs);

    // 5
    nLines -= (disasm_cur_line + 1);
    nLines = std::max(nLines, 1);

    while (iLines++ < nLines) {
      CmdCursorLineDown(0);  // nArgs
    }
    // 6
  }

  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorPageDown256(int nArgs) -> UpdateResult {
  (void)nArgs;
  const int nStep = 256;
  CursorMoveDownAligned(nStep);
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorPageDown4K(int nArgs) -> UpdateResult {
  (void)nArgs;
  const int nStep = 4096;
  CursorMoveDownAligned(nStep);
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorPageUp(int nArgs) -> UpdateResult {
  (void)nArgs;
  int iLines = 0;  // show at least 1 line from previous display
  int nLines = WindowGetHeight(window_this);

  nLines = std::max(nLines, 2);

  if (window_this == WINDOW_DATA) {
    const int nStep = 128;
    CursorMoveUpAligned(nStep);
  } else {
    //    while (++iLines < nLines)
    //      CmdCursorLineUp(nArgs);
    nLines -= (disasm_cur_line + 1);
    nLines = std::max(nLines, 1);

    while (iLines++ < nLines) {
      CmdCursorLineUp(0);  // smart line up
      // CmdCursorLineUp( -nLines );
    }
  }

  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorPageUp256(int nArgs) -> UpdateResult {
  (void)nArgs;
  const int nStep = 256;
  CursorMoveUpAligned(nStep);
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorPageUp4K(int nArgs) -> UpdateResult {
  (void)nArgs;
  const int nStep = 4096;
  CursorMoveUpAligned(nStep);
  return UPDATE_DISASM;
}

//===========================================================================
auto CmdCursorSetPC(int nArgs) -> UpdateResult  // TODO rename
{
  (void)nArgs;
  cpu_get_registers()->pc =
      disasm_cur_address;  // set PC to current cursor address
  return UPDATE_DISASM;
}

// Flags
// __________________________________________________________________________________________

auto CmdViewOutput_Text4X(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_X, VF_TEXT);
}
auto CmdViewOutput_Text41(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_1, VF_TEXT);
}
auto CmdViewOutput_Text42(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_2, VF_TEXT);
}
// Text 80
auto CmdViewOutput_Text8X(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_X, VF_TEXT | VF_80COL);
}
auto CmdViewOutput_Text81(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_1, VF_TEXT | VF_80COL);
}
auto CmdViewOutput_Text82(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_2, VF_TEXT | VF_80COL);
}
// Lo-Res
auto CmdViewOutput_GRX(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_X, 0);
}
auto CmdViewOutput_GR1(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_1, 0);
}
auto CmdViewOutput_GR2(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_2, 0);
}
// Double Lo-Res
auto CmdViewOutput_DGRX(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_X, VF_DHIRES | VF_80COL);
}
auto CmdViewOutput_DGR1(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_1, VF_DHIRES | VF_80COL);
}
auto CmdViewOutput_DGR2(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_2, VF_DHIRES | VF_80COL);
}
// Hi-Res
auto CmdViewOutput_HGRX(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_X, VF_HIRES);
}
auto CmdViewOutput_HGR1(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_1, VF_HIRES);
}
auto CmdViewOutput_HGR2(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_2, VF_HIRES);
}
// Double Hi-Res
auto CmdViewOutput_DHGRX(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_X, VF_HIRES | VF_DHIRES | VF_80COL);
}
auto CmdViewOutput_DHGR1(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_1, VF_HIRES | VF_DHIRES | VF_80COL);
}
auto CmdViewOutput_DHGR2(int nArgs) -> UpdateResult {
  (void)nArgs;
  return ViewOutput(VIEW_PAGE_2, VF_HIRES | VF_DHIRES | VF_80COL);
}

// Watches
// ________________________________________________________________________________________

auto ViewOutput(ViewVideoPage iPage, int bVideoModeFlags) -> UpdateResult {
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
