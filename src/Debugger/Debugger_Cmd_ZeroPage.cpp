// SPDX-License-Identifier: GPL-2.0-only
#include "Debugger_Cmd_ZeroPage.h"

#include <cstdint>
#include <cstdio>

#include "Debug.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"

// Globals originally from Debug.cpp

// Implementation helpers
static auto ZeroPage_Error() -> UpdateResult {
  char sText[CONSOLE_WIDTH];
  snprintf(sText, sizeof(sText),
           "  There are no current (ZP) pointers.  (Max: %d)",
           MAX_ZEROPAGE_POINTERS);
  return console_display_error(sText);
}

//===========================================================================
auto CmdZeroPage(int nArgs) -> UpdateResult {
  // ZP [address]
  // ZP # address
  return CmdZeroPageAdd(nArgs);
}

//===========================================================================
auto CmdZeroPageAdd(int nArgs) -> UpdateResult {
  // ZP [address]
  // ZP # address [address...]
  if (nArgs == 0) {
    return CmdZeroPageList(0);
  }

  int iArg = 1;
  int iZP = NO_6502_TARGET;

  if (nArgs > 1) {
    iZP = args[1].nValue;
    iArg++;
  }

  bool bAdded = false;
  for (; iArg <= nArgs; iArg++) {
    uint16_t address = args[iArg].nValue;

    if (iZP == NO_6502_TARGET) {
      iZP = 0;
      while ((iZP < MAX_ZEROPAGE_POINTERS) && zero_page_pointers[iZP].bSet) {
        iZP++;
      }
    }

    if ((iZP >= MAX_ZEROPAGE_POINTERS) && !bAdded) {
      char sText[CONSOLE_WIDTH];
      snprintf(sText, sizeof(sText),
               "All zero page pointers are currently in use.  (Max: %d)",
               MAX_ZEROPAGE_POINTERS);
      ConsoleDisplayPush(sText);
      return ConsoleUpdate();
    }

    if ((iZP < MAX_ZEROPAGE_POINTERS) &&
        (zero_page_pointers_count < MAX_ZEROPAGE_POINTERS)) {
      zero_page_pointers[iZP].bSet = true;
      zero_page_pointers[iZP].bEnabled = true;
      zero_page_pointers[iZP].address = static_cast<uint8_t>(address);
      bAdded = true;
      zero_page_pointers_count++;
      iZP++;
    }
  }

  if (!bAdded) {
    return Help_Arg_1(CMD_ZEROPAGE_POINTER_ADD);
  }

  return UPDATE_ZERO_PAGE | ConsoleUpdate();
}

//===========================================================================
auto CmdZeroPageClear(int nArgs) -> UpdateResult {
  if (zero_page_pointers_count == 0) {
    return ZeroPage_Error();
  }

  // CHECK FOR ERRORS
  if (nArgs == 0) {
    return Help_Arg_1(CMD_ZEROPAGE_POINTER_CLEAR);
  }

  bwz_ClearViaArgs(nArgs, (Breakpoint*)zero_page_pointers,
                   MAX_ZEROPAGE_POINTERS, zero_page_pointers_count);

  if (zero_page_pointers_count == 0) {
    UpdateDisplay(UPDATE_BACKGROUND);
    return UPDATE_CONSOLE_DISPLAY;
  }

  return UPDATE_CONSOLE_DISPLAY | UPDATE_ZERO_PAGE;
}

//===========================================================================
auto CmdZeroPageDisable(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    return Help_Arg_1(CMD_ZEROPAGE_POINTER_DISABLE);
  }
  if (zero_page_pointers_count == 0) {
    return ZeroPage_Error();
  }

  bwz_EnableDisableViaArgs(nArgs, (Breakpoint*)zero_page_pointers,
                           MAX_ZEROPAGE_POINTERS, false);

  return UPDATE_ZERO_PAGE;
}

//===========================================================================
auto CmdZeroPageEnable(int nArgs) -> UpdateResult {
  if (zero_page_pointers_count == 0) {
    return ZeroPage_Error();
  }

  if (nArgs == 0) {
    return Help_Arg_1(CMD_ZEROPAGE_POINTER_ENABLE);
  }

  bwz_EnableDisableViaArgs(nArgs, (Breakpoint*)zero_page_pointers,
                           MAX_ZEROPAGE_POINTERS, true);

  return UPDATE_ZERO_PAGE;
}

//===========================================================================
auto CmdZeroPageList(int nArgs) -> UpdateResult {
  (void)nArgs;
  if (zero_page_pointers_count == 0) {
    ZeroPage_Error();
  } else {
    bwz_ListAll((Breakpoint*)zero_page_pointers, MAX_ZEROPAGE_POINTERS);
  }
  return ConsoleUpdate();
}

//===========================================================================
auto CmdZeroPageSave(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdZeroPagePointer(int nArgs) -> UpdateResult {
  // p[0..4]                : disable
  // p[0..4] <ZeroPageAddr> : enable

  if ((nArgs != 0) && (nArgs != 1)) {
    return Help_Arg_1(command);
  }

  int iZP = command - CMD_ZEROPAGE_POINTER_0;

  if ((iZP < 0) || (iZP >= MAX_ZEROPAGE_POINTERS)) {
    return Help_Arg_1(command);
  }

  if (nArgs == 0) {
    zero_page_pointers[iZP].bEnabled = false;
  } else {
    zero_page_pointers[iZP].bSet = true;
    zero_page_pointers[iZP].bEnabled = true;

    uint16_t address = args[1].nValue;
    zero_page_pointers[iZP].address = static_cast<uint8_t>(address);
  }

  return UPDATE_ZERO_PAGE;
}
