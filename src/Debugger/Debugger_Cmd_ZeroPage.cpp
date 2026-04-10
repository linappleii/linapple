#include "Common.h"
#include "Debugger_Cmd_ZeroPage.h"
#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Debugger_Breakpoints.h"
#include "Log.h"
#include "Util_Text.h"
#include <cstddef>
#include <cstdio>

// Globals originally from Debug.cpp
extern ZeroPagePointers_t g_aZeroPagePointers[ MAX_ZEROPAGE_POINTERS ];
extern int g_nZeroPagePointers;

// Implementation helpers
Update_t _ZeroPage_Error()
{
  char sText[ CONSOLE_WIDTH ];
  sprintf( sText, "  There are no current (ZP) pointers.  (Max: %d)", MAX_ZEROPAGE_POINTERS );
  return ConsoleDisplayError( sText );
}

//===========================================================================
Update_t CmdZeroPage (int nArgs)
{
  // ZP [address]
  // ZP # address
  return CmdZeroPageAdd( nArgs );
}

//===========================================================================
Update_t CmdZeroPageAdd     (int nArgs)
{
  // ZP [address]
  // ZP # address [address...]
  if (! nArgs)
  {
    return CmdZeroPageList( 0 );
  }

  int iArg = 1;
  int iZP = NO_6502_TARGET;

  if (nArgs > 1)
  {
    iZP = g_aArgs[ 1 ].nValue;
    iArg++;
  }

  bool bAdded = false;
  for (; iArg <= nArgs; iArg++ )
  {
    unsigned short nAddress = g_aArgs[iArg].nValue;

    if (iZP == NO_6502_TARGET)
    {
      iZP = 0;
      while ((iZP < MAX_ZEROPAGE_POINTERS) && (g_aZeroPagePointers[iZP].bSet))
      {
        iZP++;
      }
    }

    if ((iZP >= MAX_ZEROPAGE_POINTERS) && !bAdded)
    {
      char sText[ CONSOLE_WIDTH ];
      sprintf( sText, "All zero page pointers are currently in use.  (Max: %d)", MAX_ZEROPAGE_POINTERS );
      ConsoleDisplayPush( sText );
      return ConsoleUpdate();
    }

    if ((iZP < MAX_ZEROPAGE_POINTERS) && (g_nZeroPagePointers < MAX_ZEROPAGE_POINTERS))
    {
      g_aZeroPagePointers[iZP].bSet = true;
      g_aZeroPagePointers[iZP].bEnabled = true;
      g_aZeroPagePointers[iZP].nAddress = (unsigned char) nAddress;
      bAdded = true;
      g_nZeroPagePointers++;
      iZP++;
    }
  }

  if (!bAdded)
    goto _Help;

  return UPDATE_ZERO_PAGE | ConsoleUpdate();

_Help:
  return Help_Arg_1( CMD_ZEROPAGE_POINTER_ADD );
}

//===========================================================================
Update_t CmdZeroPageClear   (int nArgs)
{
  if (!g_nZeroPagePointers)
    return _ZeroPage_Error();

  // CHECK FOR ERRORS
  if (!nArgs)
    return Help_Arg_1( CMD_ZEROPAGE_POINTER_CLEAR );

  _BWZ_ClearViaArgs( nArgs, (Breakpoint_t*)g_aZeroPagePointers, MAX_ZEROPAGE_POINTERS, g_nZeroPagePointers );

  if (! g_nZeroPagePointers)
  {
    UpdateDisplay( UPDATE_BACKGROUND );
    return UPDATE_CONSOLE_DISPLAY;
  }

  return UPDATE_CONSOLE_DISPLAY | UPDATE_ZERO_PAGE;
}

//===========================================================================
Update_t CmdZeroPageDisable (int nArgs)
{
  if (!nArgs)
    return Help_Arg_1( CMD_ZEROPAGE_POINTER_DISABLE );
  if (! g_nZeroPagePointers)
    return _ZeroPage_Error();

  _BWZ_EnableDisableViaArgs( nArgs, (Breakpoint_t*)g_aZeroPagePointers, MAX_ZEROPAGE_POINTERS, false );

  return UPDATE_ZERO_PAGE;
}

//===========================================================================
Update_t CmdZeroPageEnable  (int nArgs)
{
  if (! g_nZeroPagePointers)
    return _ZeroPage_Error();

  if (!nArgs)
    return Help_Arg_1( CMD_ZEROPAGE_POINTER_ENABLE );

  _BWZ_EnableDisableViaArgs( nArgs, (Breakpoint_t*)g_aZeroPagePointers, MAX_ZEROPAGE_POINTERS, true );

  return UPDATE_ZERO_PAGE;
}

//===========================================================================
Update_t CmdZeroPageList    (int nArgs)
{
  (void)nArgs;
  if (! g_nZeroPagePointers)
  {
    _ZeroPage_Error();
  }
  else
  {
    _BWZ_ListAll( (Breakpoint_t*)g_aZeroPagePointers, MAX_ZEROPAGE_POINTERS );
  }
  return ConsoleUpdate();
}

//===========================================================================
Update_t CmdZeroPageSave    (int nArgs)
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdZeroPagePointer (int nArgs)
{
  // p[0..4]                : disable
  // p[0..4] <ZeroPageAddr> : enable

  if( (nArgs != 0) && (nArgs != 1) )
    return Help_Arg_1( g_iCommand );

  int iZP = g_iCommand - CMD_ZEROPAGE_POINTER_0;

  if( (iZP < 0) || (iZP >= MAX_ZEROPAGE_POINTERS) )
    return Help_Arg_1( g_iCommand );

  if (nArgs == 0)
  {
    g_aZeroPagePointers[iZP].bEnabled = false;
  }
  else
  {
    g_aZeroPagePointers[iZP].bSet = true;
    g_aZeroPagePointers[iZP].bEnabled = true;

    unsigned short nAddress = g_aArgs[1].nValue;
    g_aZeroPagePointers[iZP].nAddress = (unsigned char) nAddress;
  }

  return UPDATE_ZERO_PAGE;
}
