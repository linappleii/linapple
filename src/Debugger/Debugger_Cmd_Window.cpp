#include "Common.h"
#include "Debugger_Cmd_Window.h"
#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Debugger_Color.h"
#include "Debugger_Assembler.h"
#include "Log.h"
#include "Video.h"
#include "Util_Text.h"
#include "apple2/CPU.h"
#include "SDL3/SDL.h"
#include <cstddef>
#include <cassert>

// Globals originally from Debug.cpp
extern int           g_iWindowLast;
extern int           g_iWindowThis;
extern WindowSplit_t g_aWindowConfig[ NUM_WINDOWS ];

extern int g_nConsoleDisplayLines;
extern bool g_bConsoleFullWidth;
extern int g_nConsoleDisplayWidth;
extern int g_nDisasmWinHeight;
extern int g_nDisasmCurLine;

extern unsigned short g_nDisasmTopAddress;
extern unsigned short g_nDisasmBotAddress;
extern unsigned short g_nDisasmCurAddress;
extern bool g_bDisasmCurBad;

extern unsigned int g_uVideoMode;

const int MIN_DISPLAY_CONSOLE_LINES = 5;

// Implementation

//===========================================================================
void _WindowJoin ()
{
  g_aWindowConfig[ g_iWindowThis ].bSplit = false;
}

//===========================================================================
void _WindowSplit ( Window_e eNewBottomWindow )
{
  g_aWindowConfig[ g_iWindowThis ].bSplit = true;
  g_aWindowConfig[ g_iWindowThis ].eBot = eNewBottomWindow;
}

//===========================================================================
void _WindowLast ()
{
  int eNew = g_iWindowLast;
  g_iWindowLast = g_iWindowThis;
  g_iWindowThis = eNew;
}

//===========================================================================
void _WindowSwitch( int eNewWindow )
{
  g_iWindowLast = g_iWindowThis;
  g_iWindowThis = eNewWindow;
}

//===========================================================================
Update_t _CmdWindowViewCommon ( int iNewWindow )
{
  // Switching to same window, remove split
  if (g_iWindowThis == iNewWindow)
  {
    g_aWindowConfig[ iNewWindow ].bSplit = false;
  }
  else
  {
    _WindowSwitch( iNewWindow );
  }

  WindowUpdateSizes();
  return UPDATE_ALL;
}

//===========================================================================
Update_t _CmdWindowViewFull ( int iNewWindow )
{
  if (g_iWindowThis != iNewWindow)
  {
    g_aWindowConfig[ iNewWindow ].bSplit = false;
    _WindowSwitch( iNewWindow );
    WindowUpdateConsoleDisplayedSize();
  }
  return UPDATE_ALL;
}

//===========================================================================
void WindowUpdateConsoleDisplayedSize()
{
  g_nConsoleDisplayLines = MIN_DISPLAY_CONSOLE_LINES;
#if USE_APPLE_FONT
  g_bConsoleFullWidth = true;
  g_nConsoleDisplayWidth = CONSOLE_WIDTH - 1;

  if (g_iWindowThis == WINDOW_CONSOLE)
  {
    g_nConsoleDisplayLines = MAX_DISPLAY_LINES;
    g_nConsoleDisplayWidth = CONSOLE_WIDTH - 1;
    g_bConsoleFullWidth = true;
  }
#else
  g_nConsoleDisplayWidth = (CONSOLE_WIDTH / 2) + 10;
  g_bConsoleFullWidth = false;

  if (g_iWindowThis == WINDOW_CONSOLE)
  {
    g_nConsoleDisplayLines = MAX_DISPLAY_LINES;
    g_nConsoleDisplayWidth = CONSOLE_WIDTH - 1;
    g_bConsoleFullWidth = true;
  }
#endif
}

//===========================================================================
int WindowGetHeight( int iWindow )
{
  (void)iWindow;
  return g_nDisasmWinHeight;
}

//===========================================================================
void WindowUpdateDisasmSize()
{
  if (g_aWindowConfig[ g_iWindowThis ].bSplit)
  {
    g_nDisasmWinHeight = (MAX_DISPLAY_LINES - g_nConsoleDisplayLines) / 2;
  }
  else
  {
    g_nDisasmWinHeight = MAX_DISPLAY_LINES - g_nConsoleDisplayLines;
  }
  g_nDisasmCurLine = MAX(0, (g_nDisasmWinHeight - 1) / 2);
}

//===========================================================================
void WindowUpdateSizes()
{
  WindowUpdateDisasmSize();
  WindowUpdateConsoleDisplayedSize();
}

//===========================================================================
Update_t CmdWindowCycleNext( int nArgs )
{
  (void)nArgs;
  g_iWindowThis++;
  if (g_iWindowThis >= NUM_WINDOWS)
    g_iWindowThis = 0;

  WindowUpdateSizes();

  return UPDATE_ALL;
}

//===========================================================================
Update_t CmdWindowCyclePrev( int nArgs )
{
  (void)nArgs;
  g_iWindowThis--;
  if (g_iWindowThis < 0)
    g_iWindowThis = NUM_WINDOWS-1;

  WindowUpdateSizes();

  return UPDATE_ALL;
}

//===========================================================================
Update_t CmdWindowShowCode (int nArgs)
{
  (void)nArgs;

  if (g_iWindowThis == WINDOW_CODE)
  {
    g_aWindowConfig[ g_iWindowThis ].bSplit = false;
    g_aWindowConfig[ g_iWindowThis ].eBot = WINDOW_CODE; // not really needed, but SAFE HEX ;-)
  }
  else
  if (g_iWindowThis == WINDOW_DATA)
  {
    g_aWindowConfig[ g_iWindowThis ].bSplit = true;
    g_aWindowConfig[ g_iWindowThis ].eBot = WINDOW_CODE;
  }

  WindowUpdateSizes();

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdWindowShowCode1 (int nArgs)
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdWindowShowCode2 (int nArgs)
{
  (void)nArgs;
  if ((g_iWindowThis == WINDOW_CODE) || (g_iWindowThis == WINDOW_DATA))
  {
    if (g_iWindowThis == WINDOW_CODE)
    {
      _WindowJoin();
      WindowUpdateDisasmSize();
    }
    else
    if (g_iWindowThis == WINDOW_DATA)
    {
      _WindowSplit( WINDOW_CODE );
      WindowUpdateDisasmSize();
    }
    return UPDATE_DISASM;

  }
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdWindowShowData (int nArgs)
{
  (void)nArgs;
  if (g_iWindowThis == WINDOW_CODE)
  {
    g_aWindowConfig[ g_iWindowThis ].bSplit = true;
    g_aWindowConfig[ g_iWindowThis ].eBot = WINDOW_DATA;
    return UPDATE_ALL;
  }
  else
  if (g_iWindowThis == WINDOW_DATA)
  {
    g_aWindowConfig[ g_iWindowThis ].bSplit = false;
    g_aWindowConfig[ g_iWindowThis ].eBot = WINDOW_DATA; // not really needed, but SAFE HEX ;-)
    return UPDATE_ALL;
  }

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdWindowShowData1 (int nArgs)
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdWindowShowData2 (int nArgs)
{
  (void)nArgs;
  if ((g_iWindowThis == WINDOW_CODE) || (g_iWindowThis == WINDOW_DATA))
  {
    if (g_iWindowThis == WINDOW_CODE)
    {
      _WindowSplit( WINDOW_DATA );
    }
    else
    if (g_iWindowThis == WINDOW_DATA)
    {
      _WindowJoin();
    }
    return UPDATE_DISASM;

  }
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdWindowShowSource (int nArgs)
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdWindowShowSource1 (int nArgs)
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdWindowShowSource2 (int nArgs)
{
  (void)nArgs;
  _WindowSplit( WINDOW_SOURCE );
  WindowUpdateSizes();

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdWindowViewCode (int nArgs)
{
  (void)nArgs;
  return _CmdWindowViewCommon( WINDOW_CODE );
}

//===========================================================================
Update_t CmdWindowViewConsole (int nArgs)
{
  (void)nArgs;
  return _CmdWindowViewFull( WINDOW_CONSOLE );
}

//===========================================================================
Update_t CmdWindowViewData (int nArgs)
{
  (void)nArgs;
  return _CmdWindowViewCommon( WINDOW_DATA );
}

//===========================================================================
Update_t CmdWindowViewOutput (int nArgs)
{
  (void)nArgs;
  VideoRedrawScreen();

  DebugVideoMode::Instance().Set(g_uVideoMode);

  return UPDATE_NOTHING; // intentional
}

//===========================================================================
Update_t CmdWindowViewSource (int nArgs)
{
  (void)nArgs;
  return _CmdWindowViewFull( WINDOW_CONSOLE );
}

//===========================================================================
Update_t CmdWindowViewSymbols (int nArgs)
{
  (void)nArgs;
  return _CmdWindowViewFull( WINDOW_CONSOLE );
}

//===========================================================================
Update_t CmdWindow (int nArgs)
{
  if (!nArgs)
    return Help_Arg_1( CMD_WINDOW );

  int iParam;
  char *pName = g_aArgs[1].sArg;
  int nFound = FindParam( pName, MATCH_EXACT, iParam, _PARAM_WINDOW_BEGIN, _PARAM_WINDOW_END );
  if (nFound)
  {
    switch (iParam)
    {
      case PARAM_CODE   : return CmdWindowViewCode(0)   ; break;
      case PARAM_CONSOLE: return CmdWindowViewConsole(0); break;
      case PARAM_DATA   : return CmdWindowViewData(0)   ; break;
      case PARAM_SOURCE : return CmdWindowViewSource(0) ; break;
      case PARAM_SYMBOLS: return CmdWindowViewSymbols(0); break;
      default:
        return Help_Arg_1( CMD_WINDOW );
        break;
    }
  }

  WindowUpdateConsoleDisplayedSize();

  return UPDATE_ALL;
}

//===========================================================================
Update_t CmdWindowLast (int nArgs)
{
  (void)nArgs;
  _WindowLast();
  WindowUpdateConsoleDisplayedSize();
  return UPDATE_ALL;
}

void _CursorMoveDownAligned( int nDelta )
{
  g_nDisasmCurAddress = DisasmCalcAddressFromLines( g_nDisasmCurAddress, nDelta );
}

void _CursorMoveUpAligned( int nDelta )
{
  g_nDisasmCurAddress = DisasmCalcAddressFromLines( g_nDisasmCurAddress, -nDelta );
}

//===========================================================================
void DisasmCalcTopFromCurAddress( bool bUpdateTop )
{
  (void)bUpdateTop;
  int nLen = ((g_nDisasmWinHeight - g_nDisasmCurLine) * 3); // max 3 opcodes/instruction, is our search window

  // Look for a start address that when disassembled,
  // will have the cursor on the specified line and address
  int iTop = g_nDisasmCurAddress - nLen;
  int iCur = g_nDisasmCurAddress;

  g_bDisasmCurBad = false;

  bool bFound = false;
  while (iTop <= iCur)
  {
    unsigned short iAddress = (unsigned short)iTop;
    int iOpmode;
    int nOpbytes;

    for( int iLine = 0; iLine <= nLen; iLine++ ) // min 1 opcode/instruction
    {
      _6502_GetOpmodeOpbyte( iAddress, iOpmode, nOpbytes );

      if (iLine == g_nDisasmCurLine)
      {
        if (iAddress == g_nDisasmCurAddress)
        {
          g_nDisasmTopAddress = (unsigned short)iTop;
          bFound = true;
          break;
        }
      }
      iAddress += nOpbytes;
    }
    if (bFound)
    {
      break;
    }
    iTop++;
  }

  if (! bFound)
  {
    g_nDisasmTopAddress = g_nDisasmCurAddress;
    g_bDisasmCurBad = true;
  }
}

//===========================================================================
unsigned short DisasmCalcAddressFromLines( unsigned short iAddress, int nLines )
{
  while (nLines-- > 0)
  {
    int iOpmode;
    int nOpbytes;
    _6502_GetOpmodeOpbyte( iAddress, iOpmode, nOpbytes );
    iAddress += nOpbytes;
  }
  return iAddress;
}

//===========================================================================
void DisasmCalcCurFromTopAddress()
{
  g_nDisasmCurAddress = DisasmCalcAddressFromLines( g_nDisasmTopAddress, g_nDisasmCurLine );
}

//===========================================================================
void DisasmCalcBotFromTopAddress( )
{
  g_nDisasmBotAddress = DisasmCalcAddressFromLines( g_nDisasmTopAddress, g_nDisasmWinHeight );
}

//===========================================================================
void DisasmCalcTopBotAddress ()
{
  DisasmCalcTopFromCurAddress();
  DisasmCalcBotFromTopAddress();
}

bool DebugGetVideoMode(unsigned int* pVideoMode)
{
  return DebugVideoMode::Instance().Get(pVideoMode);
}
Update_t CmdCursorFollowTarget ( int nArgs )
{
  unsigned short nAddress = 0;
  if (_6502_GetTargetAddress( g_nDisasmCurAddress, nAddress ))
  {
    g_nDisasmCurAddress = nAddress;

    if (CURSOR_ALIGN_CENTER == nArgs)
    {
      WindowUpdateDisasmSize();
    }
    else
    if (CURSOR_ALIGN_TOP == nArgs)
    {
      g_nDisasmCurLine = 0;
    }
    DisasmCalcTopBotAddress();
  }

  return UPDATE_ALL;
}


Update_t CmdCursorLineUp (int nArgs)
{
  if (g_iWindowThis == WINDOW_DATA)
  {
    _CursorMoveUpAligned( WINDOW_DATA_BYTES_PER_LINE );
    DisasmCalcTopBotAddress();
  }
  else
  if (nArgs)
  {
    g_nDisasmTopAddress--;
    DisasmCalcCurFromTopAddress();
    DisasmCalcBotFromTopAddress();
  }
  else
  {
    g_nDisasmTopAddress--;
    DisasmCalcCurFromTopAddress();
    DisasmCalcBotFromTopAddress();
  }
  return UPDATE_DISASM;
}


//===========================================================================
Update_t CmdCursorLineDown (int nArgs)
{
  int iOpmode;
  int nOpbytes;
  _6502_GetOpmodeOpbyte( g_nDisasmCurAddress, iOpmode, nOpbytes ); // g_nDisasmTopAddress

  if (g_iWindowThis == WINDOW_DATA)
  {
    _CursorMoveDownAligned( WINDOW_DATA_BYTES_PER_LINE );
    DisasmCalcTopBotAddress();
  }
  else
  if (nArgs) // scroll down by 'n' bytes
  {
    nOpbytes = nArgs; // HACKL g_aArgs[1].val

    g_nDisasmTopAddress += nOpbytes;
    g_nDisasmCurAddress += nOpbytes;
    g_nDisasmBotAddress += nOpbytes;
    DisasmCalcTopBotAddress();
  }
  else
  {
#if DEBUG_SCROLL == 6
    // Works except on one case: G FB53, SPACE, DOWN
    unsigned short nTop = g_nDisasmTopAddress;
    unsigned short nCur = g_nDisasmCurAddress + nOpbytes;
    if (g_bDisasmCurBad)
    {
      g_nDisasmCurAddress = nCur;
      g_bDisasmCurBad = false;
      DisasmCalcTopFromCurAddress();
      return UPDATE_DISASM;
    }

    // Adjust Top until nNewCur is at > Cur
    do
    {
      g_nDisasmTopAddress++;
      DisasmCalcCurFromTopAddress();
    } while (g_nDisasmCurAddress < nCur);

    DisasmCalcCurFromTopAddress();
    DisasmCalcBotFromTopAddress();
    g_bDisasmCurBad = false;
#endif
    g_nDisasmCurAddress += nOpbytes;

    _6502_GetOpmodeOpbyte( g_nDisasmTopAddress, iOpmode, nOpbytes );
    g_nDisasmTopAddress += nOpbytes;

    _6502_GetOpmodeOpbyte( g_nDisasmBotAddress, iOpmode, nOpbytes );
    g_nDisasmBotAddress += nOpbytes;

    if (g_bDisasmCurBad)
    {
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
    struct LookAhead_t
    {
      int _nAddress;
      int _iOpcode;
      int _iOpmode;
      int _nOpbytes;
    };

Update_t CmdCursorJumpPC (int nArgs)
{
  // TODO: Allow user to decide if they want next g_aOpcodes at
  // 1) Centered (traditionaly), or
  // 2) Top of the screen

  // if (UserPrefs.bNextInstructionCentered)
  if (CURSOR_ALIGN_CENTER == nArgs)
  {
    g_nDisasmCurAddress = regs.pc;       // (2)
    WindowUpdateDisasmSize(); // calc cur line
  }
  else
  if (CURSOR_ALIGN_TOP == nArgs)
  {
    g_nDisasmCurAddress = regs.pc;       // (2)
    g_nDisasmCurLine = 0;
  }

  DisasmCalcTopBotAddress();

  return UPDATE_ALL;
}


//===========================================================================
Update_t CmdCursorJumpRetAddr (int nArgs)
{
  unsigned short nAddress = 0;
  if (_6502_GetStackReturnAddress( nAddress ))
  {
    g_nDisasmCurAddress = nAddress;

    if (CURSOR_ALIGN_CENTER == nArgs)
    {
      WindowUpdateDisasmSize();
    }
    else
    if (CURSOR_ALIGN_TOP == nArgs)
    {
      g_nDisasmCurLine = 0;
    }
    DisasmCalcTopBotAddress();
  }

  return UPDATE_ALL;
}


Update_t CmdCursorPageDown (int nArgs)
{
  (void)nArgs;
  int iLines = 0; // show at least 1 line from previous display
  int nLines = WindowGetHeight( g_iWindowThis );

  if (nLines < 2)
    nLines = 2;

  if (g_iWindowThis == WINDOW_DATA)
  {
    const int nStep = 128;
    _CursorMoveDownAligned( nStep );
  }
  else
  {
// 4
//    while (++iLines < nLines)
//      CmdCursorLineDown(nArgs);

// 5
    nLines -= (g_nDisasmCurLine + 1);
    if (nLines < 1)
      nLines = 1;

    while (iLines++ < nLines)
    {
      CmdCursorLineDown( 0 ); // nArgs
    }
// 6

  }

  return UPDATE_DISASM;
}


//===========================================================================
Update_t CmdCursorPageDown256 (int nArgs)
{
  (void)nArgs;
  const int nStep = 256;
  _CursorMoveDownAligned( nStep );
  return UPDATE_DISASM;
}

//===========================================================================
Update_t CmdCursorPageDown4K (int nArgs)
{
  (void)nArgs;
  const int nStep = 4096;
  _CursorMoveDownAligned( nStep );
  return UPDATE_DISASM;
}

//===========================================================================
Update_t CmdCursorPageUp (int nArgs)
{
  (void)nArgs;
  int iLines = 0; // show at least 1 line from previous display
  int nLines = WindowGetHeight( g_iWindowThis );

  if (nLines < 2)
    nLines = 2;

  if (g_iWindowThis == WINDOW_DATA)
  {
    const int nStep = 128;
    _CursorMoveUpAligned( nStep );
  }
  else
  {
//    while (++iLines < nLines)
//      CmdCursorLineUp(nArgs);
    nLines -= (g_nDisasmCurLine + 1);
    if (nLines < 1)
      nLines = 1;

    while (iLines++ < nLines)
    {
      CmdCursorLineUp( 0 ); // smart line up
      // CmdCursorLineUp( -nLines );
    }
  }

  return UPDATE_DISASM;
}

//===========================================================================
Update_t CmdCursorPageUp256 (int nArgs)
{
  (void)nArgs;
  const int nStep = 256;
  _CursorMoveUpAligned( nStep );
  return UPDATE_DISASM;
}

//===========================================================================
Update_t CmdCursorPageUp4K (int nArgs)
{
  (void)nArgs;
  const int nStep = 4096;
  _CursorMoveUpAligned( nStep );
  return UPDATE_DISASM;
}

//===========================================================================
Update_t CmdCursorSetPC( int nArgs) // TODO rename
{
  (void)nArgs;
  regs.pc = g_nDisasmCurAddress; // set PC to current cursor address
  return UPDATE_DISASM;
}


// Flags __________________________________________________________________________________________


  Update_t CmdViewOutput_Text4X (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_X, VF_TEXT );
  }
  Update_t CmdViewOutput_Text41 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_1, VF_TEXT );
  }
  Update_t CmdViewOutput_Text42 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_2, VF_TEXT );
  }
// Text 80
  Update_t CmdViewOutput_Text8X (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_X, VF_TEXT | VF_80COL );
  }
  Update_t CmdViewOutput_Text81 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_1, VF_TEXT | VF_80COL );
  }
  Update_t CmdViewOutput_Text82 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_2, VF_TEXT | VF_80COL );
  }
// Lo-Res
  Update_t CmdViewOutput_GRX (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_X, 0 );
  }
  Update_t CmdViewOutput_GR1 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_1, 0 );
  }
  Update_t CmdViewOutput_GR2 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_2, 0 );
  }
// Double Lo-Res
  Update_t CmdViewOutput_DGRX (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_X, VF_DHIRES | VF_80COL );
  }
  Update_t CmdViewOutput_DGR1 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_1, VF_DHIRES | VF_80COL );
  }
  Update_t CmdViewOutput_DGR2 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_2, VF_DHIRES | VF_80COL );
  }
// Hi-Res
  Update_t CmdViewOutput_HGRX (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_X, VF_HIRES );
  }
  Update_t CmdViewOutput_HGR1 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_1, VF_HIRES );
  }
  Update_t CmdViewOutput_HGR2 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_2, VF_HIRES );
  }
// Double Hi-Res
  Update_t CmdViewOutput_DHGRX (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_X, VF_HIRES | VF_DHIRES | VF_80COL );
  }
  Update_t CmdViewOutput_DHGR1 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_1, VF_HIRES | VF_DHIRES | VF_80COL);
  }
  Update_t CmdViewOutput_DHGR2 (int nArgs)
  {
  (void)nArgs;
    return _ViewOutput( VIEW_PAGE_2, VF_HIRES | VF_DHIRES | VF_80COL );
  }

// Watches ________________________________________________________________________________________


Update_t _ViewOutput( ViewVideoPage_t iPage, int bVideoModeFlags )
{
  switch( iPage )
  {
    case VIEW_PAGE_X:
      bVideoModeFlags |= !VideoGetSWPAGE2() ? 0 : VF_PAGE2;
      bVideoModeFlags |= !VideoGetSWMIXED() ? 0 : VF_MIXED;
      break; // Page Current & current MIXED state
    case VIEW_PAGE_1: bVideoModeFlags |= 0; break; // Page 1
    case VIEW_PAGE_2: bVideoModeFlags |= VF_PAGE2; break; // Page 2
    default:
      assert(0);
      break;
  }

  DebugVideoMode::Instance().Set(bVideoModeFlags);
  VideoRefreshScreen( bVideoModeFlags, true );
  return UPDATE_NOTHING; // intentional
}

