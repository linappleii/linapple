#include "Common.h"
#include "Debugger_Types.h"
#include "Debugger_Commands.h"
#include "Debugger_Parser.h"
#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Help.h"
#include <cstring>
#include <cstddef>

extern VideoScannerDisplayInfo g_videoScannerDisplayInfo;

Update_t CmdVideoScannerInfo(int nArgs)
{
  if (nArgs != 1)
  {
    return Help_Arg_1(CMD_VIDEO_SCANNER_INFO);
  }
  else
  {
    if (strcmp(g_aArgs[1].sArg, "dec") == 0)
      g_videoScannerDisplayInfo.isDecimal = true;
    else if (strcmp(g_aArgs[1].sArg, "hex") == 0)
      g_videoScannerDisplayInfo.isDecimal = false;
    else if (strcmp(g_aArgs[1].sArg, "real") == 0)
      g_videoScannerDisplayInfo.isHorzReal = true;
    else if (strcmp(g_aArgs[1].sArg, "apple") == 0)
      g_videoScannerDisplayInfo.isHorzReal = false;
    else
      return Help_Arg_1(CMD_VIDEO_SCANNER_INFO);
  }

  char sText[CONSOLE_WIDTH];
  ConsoleBufferPushFormat(sText, "Video-scanner display updated: %s", g_aArgs[1].sArg);
  ConsoleBufferToDisplay();

  return UPDATE_ALL;
}

Update_t CmdCyclesInfo(int nArgs)
{
  if (nArgs != 1)
  {
    return Help_Arg_1(CMD_CYCLES_INFO);
  }
  else
  {
    if (strcmp(g_aArgs[1].sArg, "abs") == 0)
      g_videoScannerDisplayInfo.isAbsCycle = true;
    else if (strcmp(g_aArgs[1].sArg, "rel") == 0)
      g_videoScannerDisplayInfo.isAbsCycle = false;
    else
      return Help_Arg_1(CMD_CYCLES_INFO);
  }

  char sText[CONSOLE_WIDTH];
  ConsoleBufferPushFormat(sText, "Cycles display updated: %s", g_aArgs[1].sArg);
  ConsoleBufferToDisplay();

  return UPDATE_ALL;
}
