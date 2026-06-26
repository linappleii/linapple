#include <cctype>
#include <cstddef>

#include "Debugger_Breakpoints.h"
#include "Debugger_Commands.h"
#include "Debugger_Display.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"
#include "apple2/CPU.h"
#include "core/Common.h"

extern int g_iCommand;

auto CmdFlagClear(int nArgs) -> Update_t {
  int iFlag = (g_iCommand - CMD_FLAG_CLR_C);

  if (g_iCommand == CMD_FLAG_CLEAR) {
    int iArg = nArgs;
    while (iArg) {
      iFlag = 0;
      while (iFlag < _6502_NUM_FLAGS) {
        if (g_aBreakpointSource[BP_SRC_FLAG_N - iFlag][0] ==
            toupper(g_aArgs[iArg].sArg[0])) {
          CpuGetRegisters()->ps &= ~(1 << (7 - iFlag));
          break;
        }
        iFlag++;
      }
      iArg--;
    }
  } else {
    CpuGetRegisters()->ps &= ~(1 << iFlag);
  }

  return UPDATE_FLAGS;
}

auto CmdFlagSet(int nArgs) -> Update_t {
  int iFlag = (g_iCommand - CMD_FLAG_SET_C);

  if (g_iCommand == CMD_FLAG_SET) {
    int iArg = nArgs;
    while (iArg) {
      iFlag = 0;
      while (iFlag < _6502_NUM_FLAGS) {
        if (g_aBreakpointSource[BP_SRC_FLAG_N - iFlag][0] ==
            toupper(g_aArgs[iArg].sArg[0])) {
          CpuGetRegisters()->ps |= (1 << (7 - iFlag));
          break;
        }
        iFlag++;
      }
      iArg--;
    }
  } else {
    CpuGetRegisters()->ps |= (1 << iFlag);
  }
  return UPDATE_FLAGS;
}

auto CmdFlag(int nArgs) -> Update_t {
  if (g_iCommand == CMD_FLAG_CLEAR) {
    return CmdFlagClear(nArgs);
  } else if (g_iCommand == CMD_FLAG_SET) {
    return CmdFlagSet(nArgs);
  }

  return UPDATE_ALL;
}
