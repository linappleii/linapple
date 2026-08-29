#include <cstring>

#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Help.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"

extern VideoScannerDisplayInfo_t g_video_scanner_display_info;

auto CmdVideoScannerInfo(int nArgs) -> Update_t {
  if (nArgs != 1) {
    return Help_Arg_1(CMD_VIDEO_SCANNER_INFO);
  } else {
    if (strcmp(g_args[1].sArg, "dec") == 0) {
      g_video_scanner_display_info.isDecimal = true;
    } else if (strcmp(g_args[1].sArg, "hex") == 0) {
      g_video_scanner_display_info.isDecimal = false;
    } else if (strcmp(g_args[1].sArg, "real") == 0) {
      g_video_scanner_display_info.isHorzReal = true;
    } else if (strcmp(g_args[1].sArg, "apple") == 0) {
      g_video_scanner_display_info.isHorzReal = false;
    } else {
      return Help_Arg_1(CMD_VIDEO_SCANNER_INFO);
    }
  }

  char sText[CONSOLE_WIDTH];
  ConsoleBufferPushFormat(sText, "Video-scanner display updated: %s",
                          g_args[1].sArg);
  ConsoleBufferToDisplay();

  return UPDATE_ALL;
}

auto CmdCyclesInfo(int nArgs) -> Update_t {
  if (nArgs != 1) {
    return Help_Arg_1(CMD_CYCLES_INFO);
  } else {
    if (strcmp(g_args[1].sArg, "abs") == 0) {
      g_video_scanner_display_info.isAbsCycle = true;
    } else if (strcmp(g_args[1].sArg, "rel") == 0) {
      g_video_scanner_display_info.isAbsCycle = false;
    } else {
      return Help_Arg_1(CMD_CYCLES_INFO);
    }
  }

  char sText[CONSOLE_WIDTH];
  ConsoleBufferPushFormat(sText, "Cycles display updated: %s", g_args[1].sArg);
  ConsoleBufferToDisplay();

  return UPDATE_ALL;
}
