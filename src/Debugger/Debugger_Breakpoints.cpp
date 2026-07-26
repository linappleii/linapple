#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "Debugger_Breakpoints.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "Debug.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Debugger_Color.h"
#include "Debugger_Console.h"
#include "Debugger_DisassemblerData.h"
#include "Debugger_Assembler.h"
#include "Debugger_Range.h"
#include "core/Log.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <cstdio>

extern uint16_t g_uBreakMemoryAddress;
extern MemoryTextFile_t g_ConfigState;
extern const Opcodes_t *g_aOpcodes;
extern const Opcodes_t g_aOpcodes65C02[NUM_OPCODES];

extern int g_iDebugBreakOnOpcode;
extern int g_bDebugBreakpointHit;

int  g_nDebugBreakOnInvalid  = 0; // Bit Flags of Invalid Opcode to break on
int  g_iDebugBreakOnOpcode   = 0;

int  g_bDebugBreakpointHit = 0;  // See: BreakpointHit_t

int  g_nBreakpoints          = 0;
Breakpoint_t g_aBreakpoints[ MAX_BREAKPOINTS ];

// NOTE: BreakpointSource_t and g_aBreakpointSource must match!
const char *g_aBreakpointSource[ NUM_BREAKPOINT_SOURCES ] =
{
  "A", "X", "Y", "PC", "S", "P", "C", "Z", "I", "D", "B", "R", "V", "N", "OP", "M", "M", "M"
};

// Note: BreakpointOperator_t, _PARAM_BREAKPOINT_, and g_aBreakpointSymbols must match!
const char *g_aBreakpointSymbols[ NUM_BREAKPOINT_OPERATORS ] =
{
  "<=", "< ", "= ", "!=", "> ", ">=", "? ", "@ ", "* "
};

auto IsDebugBreakOnInvalid(int iOpcodeType) -> bool
{
  extern int g_nDebugBreakOnInvalid;
  g_bDebugBreakpointHit |= ((g_nDebugBreakOnInvalid >> iOpcodeType) & 1) ? BP_HIT_INVALID : 0;
  return g_bDebugBreakpointHit != 0;
}

void ClearTempBreakpoints()
{
  int iBP = 0;
  while (iBP < MAX_BREAKPOINTS)
  {
    if (g_aBreakpoints[ iBP ].bSet && g_aBreakpoints[ iBP ].bTemp)
    {
      _BWZ_Clear( g_aBreakpoints, iBP );
      g_nBreakpoints--;
    }
    iBP++;
  }
}

// BWZ (Breakpoint, Watch, ZeroPage) shared helpers _______________________________________________

void _BWZ_Clear( Breakpoint_t * aBreakWatchZero, int iSlot )
{
  if (aBreakWatchZero)
  {
    aBreakWatchZero[ iSlot ].bSet = false;
    aBreakWatchZero[ iSlot ].bEnabled = false;
    aBreakWatchZero[ iSlot ].bTemp = false;
    aBreakWatchZero[ iSlot ].nAddress = 0;
    aBreakWatchZero[ iSlot ].nLength = 0;
    aBreakWatchZero[ iSlot ].eSource = static_cast<BreakpointSource_t>(0);
    aBreakWatchZero[ iSlot ].eOperator = static_cast<BreakpointOperator_t>(0);
  }
}

void _BWZ_RemoveOne( Breakpoint_t *aBreakWatchZero, const int iSlot, int & nTotal )
{
  if (aBreakWatchZero)
  {
    if (aBreakWatchZero[ iSlot ].bSet)
    {
      _BWZ_Clear( aBreakWatchZero, iSlot );
      nTotal--;
    }
  }
}

void _BWZ_RemoveAll( Breakpoint_t *aBreakWatchZero, const int nMax, int & nTotal )
{
  if (aBreakWatchZero)
  {
    int i = 0;
    while (i < nMax)
    {
      _BWZ_Clear( aBreakWatchZero, i );
      i++;
    }
    nTotal = 0;
  }
}

void _BWZ_ClearViaArgs( int nArgs, Breakpoint_t * aBreakWatchZero, const int nMax, int & nTotal )
{
  if (aBreakWatchZero)
  {
    for( int iArg = 1; iArg <= nArgs; iArg++ )
    {
      int iSlot = g_aArgs[ iArg ].nValue;
      if (iSlot < nMax)
      {
        _BWZ_RemoveOne( aBreakWatchZero, iSlot, nTotal );
      }
    }
  }
}

void _BWZ_EnableDisableViaArgs( int nArgs, Breakpoint_t * aBreakWatchZero, const int nMax, const bool bEnabled )
{
  if (aBreakWatchZero)
  {
    for( int iArg = 1; iArg <= nArgs; iArg++ )
    {
      int iSlot = g_aArgs[ iArg ].nValue;
      if (iSlot < nMax)
      {
        aBreakWatchZero[ iSlot ].bEnabled = bEnabled;
      }
    }
  }
}

void _BWZ_List( const Breakpoint_t * aBreakWatchZero, const int iBWZ )
{
  if (aBreakWatchZero)
  {
    char sText[ CONSOLE_WIDTH ];
    const Breakpoint_t *pBWZ = & aBreakWatchZero[ iBWZ ];

    const char *pSrc = g_aBreakpointSource[ pBWZ->eSource ];
    const char *pCmp = g_aBreakpointSymbols[ pBWZ->eOperator ];

    sprintf( sText, "  %x: %s %s %04X", iBWZ, pSrc, pCmp, pBWZ->nAddress );
    if (pBWZ->nLength > 1)
    {
      char sLen[ 32 ];
      sprintf( sLen, ",%04X", pBWZ->nLength );
      strcat( sText, sLen );
    }

    if (! pBWZ->bEnabled)
    {
      strcat( sText, " (Disabled)" );
    }

    ConsoleBufferPush( sText );
  }
}

void _BWZ_ListAll( const Breakpoint_t * aBreakWatchZero, const int nMax )
{
  if (aBreakWatchZero)
  {
    int i = 0;
    while (i < nMax)
    {
      if (aBreakWatchZero[ i ].bSet)
      {
        _BWZ_List( aBreakWatchZero, i );
      }
      i++;
    }
  }
}

// Breakpoints ____________________________________________________________________________________

auto CmdBreakpoint(int nArgs) -> Update_t
{
  return CmdBreakpointAddSmart( nArgs );
}

auto CmdBreakpointAddSmart(int nArgs) -> Update_t
{
  if (! nArgs)
  {
    return CmdBreakpointList( 0 );
  }

  // 1. BP address
  // 2. BP register operator value
  // 3. BP register operator value,length

  if (nArgs == 1)
  {
    return CmdBreakpointAddPC( nArgs );
  }

  // Check if arg[1] is a register
  int iSrc = 0;
  if (FindParam( g_aArgs[ 1 ].sArg, MATCH_EXACT, iSrc, _PARAM_BREAKPOINT_BEGIN, _PARAM_BREAKPOINT_END ) > 0)
  {
    return CmdBreakpointAddReg( nArgs );
  }

  return CmdBreakpointAddPC( nArgs );
}

auto CmdBreakpointAddPC(int nArgs) -> Update_t
{
  if (! nArgs)
  {
    return Help_Arg_1( CMD_BREAKPOINT_ADD_PC );
  }

  for( int iArg = 1; iArg <= nArgs; iArg++ )
  {
    uint16_t nAddress = g_aArgs[ iArg ].nValue;
    _CmdBreakpointAddCommonArg( iArg, nArgs, BP_SRC_REG_PC, BP_OP_EQUAL );
    (void)nAddress;
  }

  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddReg(int nArgs) -> Update_t
{
  if (nArgs < 3)
  {
    return Help_Arg_1( CMD_BREAKPOINT_ADD_REG );
  }

  _CmdBreakpointAddCommonArg( 1, nArgs, BP_SRC_REG_A, BP_OP_EQUAL );

  return UPDATE_BREAKPOINTS;
}

int _CmdBreakpointAddCommonArg ( int iArg, int nArg, BreakpointSource_t iSrc, BreakpointOperator_t iCmp, bool bIsTempBreakpoint )
{
  (void)nArg;
  int iBP = 0;
  while ((iBP < MAX_BREAKPOINTS) && (g_aBreakpoints[ iBP ].bSet))
  {
    iBP++;
  }

  if (iBP >= MAX_BREAKPOINTS)
  {
    console_display_error( "All breakpoints are currently in use." );
    return 0;
  }

  Breakpoint_t *pBP = & g_aBreakpoints[ iBP ];
  pBP->bSet = true;
  pBP->bEnabled = true;
  pBP->bTemp = bIsTempBreakpoint;
  pBP->eSource = iSrc;
  pBP->eOperator = iCmp;
  pBP->nAddress = g_aArgs[ iArg ].nValue;
  pBP->nLength = 1;

  g_nBreakpoints++;

  return 1;
}

auto CmdBreakpointAddIO (int nArgs) -> Update_t
{
  if (nArgs < 1) return Help_Arg_1(CMD_BREAKPOINT_ADD_IO);
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddMemA (int nArgs) -> Update_t
{
  if (nArgs < 1) return Help_Arg_1(CMD_BREAKPOINT_ADD_MEM);
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddMemR (int nArgs) -> Update_t
{
  if (nArgs < 1) return Help_Arg_1(CMD_BREAKPOINT_ADD_MEMR);
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointAddMemW (int nArgs) -> Update_t
{
  if (nArgs < 1) return Help_Arg_1(CMD_BREAKPOINT_ADD_MEMW);
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointEdit (int nArgs) -> Update_t
{
  if (nArgs < 1) return Help_Arg_1(CMD_BREAKPOINT_EDIT);
  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointClear(int nArgs) -> Update_t
{
  if (! g_nBreakpoints) {
    return console_display_error("There are no breakpoints defined.");
}

  if (!nArgs)
  {
    _BWZ_RemoveAll( g_aBreakpoints, MAX_BREAKPOINTS, g_nBreakpoints );
  }
  else
  {
    _BWZ_ClearViaArgs( nArgs, g_aBreakpoints, MAX_BREAKPOINTS, g_nBreakpoints );
  }

  return UPDATE_DISASM | UPDATE_BREAKPOINTS | UPDATE_CONSOLE_DISPLAY;
}

auto CmdBreakpointDisable(int nArgs) -> Update_t
{
  if (! g_nBreakpoints) {
    return console_display_error("There are no breakpoints defined.");
}

  if (! nArgs) {
    return Help_Arg_1( CMD_BREAKPOINT_DISABLE );
}

  _BWZ_EnableDisableViaArgs( nArgs, g_aBreakpoints, MAX_BREAKPOINTS, false );

  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointEnable(int nArgs) -> Update_t
{
  if (! g_nBreakpoints) {
    return console_display_error("There are no breakpoints defined.");
}

  if (! nArgs) {
    return Help_Arg_1( CMD_BREAKPOINT_ENABLE );
}

  _BWZ_EnableDisableViaArgs( nArgs, g_aBreakpoints, MAX_BREAKPOINTS, true );

  return UPDATE_BREAKPOINTS;
}

auto CmdBreakpointList(int nArgs) -> Update_t
{
  (void)nArgs;
  if (! g_nBreakpoints)
  {
    char sText[ CONSOLE_WIDTH ];
    sprintf( sText, "  There are no current breakpoints.  (Max: %d)", MAX_BREAKPOINTS );
    ConsoleBufferPush( sText );
  }
  else
  {
    _BWZ_ListAll( g_aBreakpoints, MAX_BREAKPOINTS );
  }
  return ConsoleUpdate();
}

auto CmdBreakpointSave(int nArgs) -> Update_t
{
  char sText[ CONSOLE_WIDTH ];

  // ConfigSave_PrepareHeader( PARAM_CAT_BREAKPOINTS, CMD_BREAKPOINT_CLEAR );

  int iBreakpoint = 0;
  while (iBreakpoint < MAX_BREAKPOINTS)
  {
    if (g_aBreakpoints[ iBreakpoint ].bSet)
    {
      sprintf( sText, "%s %x %04X,%04X\n"
        , g_aCommands[ CMD_BREAKPOINT_ADD_REG ].m_sName
        , iBreakpoint
        , g_aBreakpoints[ iBreakpoint ].nAddress
        , g_aBreakpoints[ iBreakpoint ].nLength
      );
      g_ConfigState.PushLine( sText );
    }
    if (! g_aBreakpoints[ iBreakpoint ].bEnabled)
    {
      sprintf( sText, "%s %x\n"
        , g_aCommands[ CMD_BREAKPOINT_DISABLE ].m_sName
        , iBreakpoint
      );
      g_ConfigState.PushLine( sText );
    }

    iBreakpoint++;
  }

  if (nArgs)
  {
    if (! (g_aArgs[ 1 ].bType & TYPE_QUOTED_2)) {
      return Help_Arg_1( CMD_BREAKPOINT_SAVE );
}

    // if (ConfigSave_BufferToDisk( g_aArgs[ 1 ].sArg, CONFIG_SAVE_FILE_CREATE ))
    {
      ConsoleBufferPush(  "Saved."  );
      return ConsoleUpdate();
    }
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdWatch (int nArgs) -> Update_t
{
  return CmdWatchAdd( nArgs );
}

auto CmdWatchAdd (int nArgs) -> Update_t
{
  if (! nArgs)
  {
    return CmdWatchList( 0 );
  }

  int iArg = 1;
  int iWatch = NO_6502_TARGET;
  if (nArgs > 1)
  {
    iWatch = static_cast<int>(g_aArgs[ 1 ].nValue);
    iArg++;
  }

  bool bAdded = false;
  for (; iArg <= nArgs; iArg++ )
  {
    uint16_t nAddress = g_aArgs[iArg].nValue;

    if ((nAddress >= _6502_IO_BEGIN) && (nAddress <= _6502_IO_END)) {
      return console_display_error("You may not watch an I/O location.");
}

    if (iWatch == NO_6502_TARGET)
    {
      iWatch = 0;
      while ((iWatch < MAX_WATCHES) && (g_aWatches[iWatch].bSet))
      {
        iWatch++;
      }
    }

    if ((iWatch >= MAX_WATCHES) && !bAdded)
    {
      char sText[ CONSOLE_WIDTH ];
      sprintf( sText, "All watches are currently in use.  (Max: %d)", MAX_WATCHES );
      ConsoleDisplayPush( sText );
      return ConsoleUpdate();
    }

    if ((iWatch < MAX_WATCHES) && (g_nWatches < MAX_WATCHES))
    {
      g_aWatches[iWatch].bSet = true;
      g_aWatches[iWatch].bEnabled = true;
      g_aWatches[iWatch].nAddress = nAddress;
      bAdded = true;
      g_nWatches++;
      iWatch++;
    }
  }

  if (!bAdded) {
    return Help_Arg_1( CMD_WATCH_ADD );
}

  return UPDATE_WATCH;
}

auto CmdWatchSave(int nArgs) -> Update_t
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdWatchClear(int nArgs) -> Update_t
{
  if (! g_nWatches) {
    return console_display_error("There are no watches defined.");
}

  if (!nArgs)
  {
    _BWZ_RemoveAll( (Breakpoint_t*)g_aWatches, MAX_WATCHES, g_nWatches );
  }
  else
  {
    _BWZ_ClearViaArgs( nArgs, (Breakpoint_t*)g_aWatches, MAX_WATCHES, g_nWatches );
  }

  return UPDATE_WATCH | UPDATE_CONSOLE_DISPLAY;
}

auto CmdWatchDisable(int nArgs) -> Update_t
{
  if (! g_nWatches) {
    return console_display_error("There are no watches defined.");
}

  if (!nArgs) {
    return Help_Arg_1( CMD_WATCH_DISABLE );
}

  _BWZ_EnableDisableViaArgs( nArgs, (Breakpoint_t*)g_aWatches, MAX_WATCHES, false );

  return UPDATE_WATCH;
}

auto CmdWatchEnable(int nArgs) -> Update_t
{
  if (! g_nWatches) {
    return console_display_error("There are no watches defined.");
}

  if (!nArgs) {
    return Help_Arg_1( CMD_WATCH_ENABLE );
}

  _BWZ_EnableDisableViaArgs( nArgs, (Breakpoint_t*)g_aWatches, MAX_WATCHES, true );

  return UPDATE_WATCH;
}

auto CmdWatchList(int nArgs) -> Update_t
{
  (void)nArgs;
  if (! g_nWatches)
  {
    char sText[ CONSOLE_WIDTH ];
    sprintf( sText, "  There are no current watches.  (Max: %d)", MAX_WATCHES );
    ConsoleBufferPush( sText );
  }
  else
  {
    _BWZ_ListAll( (Breakpoint_t*)g_aWatches, MAX_WATCHES );
  }
  return ConsoleUpdate();
}
