#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "Debugger_Cmd_ZeroPage.h"
#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Debugger_Breakpoints.h"
#include "core/Log.h"
#include "core/Util_Text.h"
#include <cstddef>
#include <cstdio>

// Globals originally from Debug.cpp
extern ZeroPagePointers_t g_zero_page_pointers[ MAX_ZEROPAGE_POINTERS ];
extern int g_zero_page_pointers_count;

// Implementation helpers
auto _ZeroPage_Error() -> Update_t
{
  char sText[ CONSOLE_WIDTH ];
  sprintf( sText, "  There are no current (ZP) pointers.  (Max: %d)", MAX_ZEROPAGE_POINTERS );
  return console_display_error( sText );
}

//===========================================================================
auto CmdZeroPage (int nArgs) -> Update_t
{
  // ZP [address]
  // ZP # address
  return CmdZeroPageAdd( nArgs );
}

//===========================================================================
auto CmdZeroPageAdd     (int nArgs) -> Update_t
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
    iZP = g_args[ 1 ].nValue;
    iArg++;
  }

  bool bAdded = false;
  for (; iArg <= nArgs; iArg++ )
  {
    uint16_t address = g_args[iArg].nValue;

    if (iZP == NO_6502_TARGET)
    {
      iZP = 0;
      while ((iZP < MAX_ZEROPAGE_POINTERS) && (g_zero_page_pointers[iZP].bSet))
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

    if ((iZP < MAX_ZEROPAGE_POINTERS) && (g_zero_page_pointers_count < MAX_ZEROPAGE_POINTERS))
    {
      g_zero_page_pointers[iZP].bSet = true;
      g_zero_page_pointers[iZP].bEnabled = true;
      g_zero_page_pointers[iZP].address = static_cast<uint8_t>(address);
      bAdded = true;
      g_zero_page_pointers_count++;
      iZP++;
    }
  }

  if (!bAdded) {
    goto _Help;
}

  return UPDATE_ZERO_PAGE | ConsoleUpdate();

_Help:
  return Help_Arg_1( CMD_ZEROPAGE_POINTER_ADD );
}

//===========================================================================
auto CmdZeroPageClear   (int nArgs) -> Update_t
{
  if (!g_zero_page_pointers_count) {
    return _ZeroPage_Error();
}

  // CHECK FOR ERRORS
  if (!nArgs) {
    return Help_Arg_1( CMD_ZEROPAGE_POINTER_CLEAR );
}

  _BWZ_ClearViaArgs( nArgs, (Breakpoint_t*)g_zero_page_pointers, MAX_ZEROPAGE_POINTERS, g_zero_page_pointers_count );

  if (! g_zero_page_pointers_count)
  {
    UpdateDisplay( UPDATE_BACKGROUND );
    return UPDATE_CONSOLE_DISPLAY;
  }

  return UPDATE_CONSOLE_DISPLAY | UPDATE_ZERO_PAGE;
}

//===========================================================================
auto CmdZeroPageDisable (int nArgs) -> Update_t
{
  if (!nArgs) {
    return Help_Arg_1( CMD_ZEROPAGE_POINTER_DISABLE );
}
  if (! g_zero_page_pointers_count) {
    return _ZeroPage_Error();
}

  _BWZ_EnableDisableViaArgs( nArgs, (Breakpoint_t*)g_zero_page_pointers, MAX_ZEROPAGE_POINTERS, false );

  return UPDATE_ZERO_PAGE;
}

//===========================================================================
auto CmdZeroPageEnable  (int nArgs) -> Update_t
{
  if (! g_zero_page_pointers_count) {
    return _ZeroPage_Error();
}

  if (!nArgs) {
    return Help_Arg_1( CMD_ZEROPAGE_POINTER_ENABLE );
}

  _BWZ_EnableDisableViaArgs( nArgs, (Breakpoint_t*)g_zero_page_pointers, MAX_ZEROPAGE_POINTERS, true );

  return UPDATE_ZERO_PAGE;
}

//===========================================================================
auto CmdZeroPageList    (int nArgs) -> Update_t
{
  (void)nArgs;
  if (! g_zero_page_pointers_count)
  {
    _ZeroPage_Error();
  }
  else
  {
    _BWZ_ListAll( (Breakpoint_t*)g_zero_page_pointers, MAX_ZEROPAGE_POINTERS );
  }
  return ConsoleUpdate();
}

//===========================================================================
auto CmdZeroPageSave    (int nArgs) -> Update_t
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdZeroPagePointer (int nArgs) -> Update_t
{
  // p[0..4]                : disable
  // p[0..4] <ZeroPageAddr> : enable

  if( (nArgs != 0) && (nArgs != 1) ) {
    return Help_Arg_1( g_command );
}

  int iZP = g_command - CMD_ZEROPAGE_POINTER_0;

  if( (iZP < 0) || (iZP >= MAX_ZEROPAGE_POINTERS) ) {
    return Help_Arg_1( g_command );
}

  if (nArgs == 0)
  {
    g_zero_page_pointers[iZP].bEnabled = false;
  }
  else
  {
    g_zero_page_pointers[iZP].bSet = true;
    g_zero_page_pointers[iZP].bEnabled = true;

    uint16_t address = g_args[1].nValue;
    g_zero_page_pointers[iZP].address = static_cast<uint8_t>(address);
  }

  return UPDATE_ZERO_PAGE;
}
