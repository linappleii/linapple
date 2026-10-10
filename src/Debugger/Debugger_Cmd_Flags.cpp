// SPDX-License-Identifier: GPL-2.0-only
#include <cctype>

#include "Debugger_Breakpoints.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"
#include "apple2/CPU.h"

extern int command;

auto CmdFlagClear(int nArgs) -> UpdateResult {
  int iFlag = (command - CMD_FLAG_CLR_C);

  if (command == CMD_FLAG_CLEAR) {
    int iArg = nArgs;
    while (iArg != 0) {
      iFlag = 0;
      while (iFlag < DBG_6502_NUM_FLAGS) {
        if (*breakpoint_source[BP_SRC_FLAG_N - iFlag] ==
            toupper(static_cast<unsigned char>(*args[iArg].sArg))) {
          cpu_get_registers()->ps &= ~(1 << (7 - iFlag));
          break;
        }
        iFlag++;
      }
      iArg--;
    }
  } else {
    cpu_get_registers()->ps &= ~(1 << iFlag);
  }

  return UPDATE_FLAGS;
}

auto CmdFlagSet(int nArgs) -> UpdateResult {
  int iFlag = (command - CMD_FLAG_SET_C);

  if (command == CMD_FLAG_SET) {
    int iArg = nArgs;
    while (iArg != 0) {
      iFlag = 0;
      while (iFlag < DBG_6502_NUM_FLAGS) {
        if (*breakpoint_source[BP_SRC_FLAG_N - iFlag] ==
            toupper(static_cast<unsigned char>(*args[iArg].sArg))) {
          cpu_get_registers()->ps |= (1 << (7 - iFlag));
          break;
        }
        iFlag++;
      }
      iArg--;
    }
  } else {
    cpu_get_registers()->ps |= (1 << iFlag);
  }
  return UPDATE_FLAGS;
}

auto CmdFlag(int nArgs) -> UpdateResult {
  if (command == CMD_FLAG_CLEAR) {
    return CmdFlagClear(nArgs);
  }
  if (command == CMD_FLAG_SET) {
    return CmdFlagSet(nArgs);
  }

  return UPDATE_ALL;
}
