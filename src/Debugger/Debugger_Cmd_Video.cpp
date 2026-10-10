// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>

#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"

auto CmdVideoScannerInfo(int nArgs) -> UpdateResult {
  if (nArgs != 1) {
    return Help_Arg_1(CMD_VIDEO_SCANNER_INFO);
  }
  if (strcmp(args[1].sArg, "dec") == 0) {
    video_scanner_display_info.isDecimal = true;
  } else if (strcmp(args[1].sArg, "hex") == 0) {
    video_scanner_display_info.isDecimal = false;
  } else if (strcmp(args[1].sArg, "real") == 0) {
    video_scanner_display_info.isHorzReal = true;
  } else if (strcmp(args[1].sArg, "apple") == 0) {
    video_scanner_display_info.isHorzReal = false;
  } else {
    return Help_Arg_1(CMD_VIDEO_SCANNER_INFO);
  }

  char sText[CONSOLE_WIDTH];
  ConsoleBufferPushFormat(sText, "Video-scanner display updated: %s",
                          args[1].sArg);
  ConsoleBufferToDisplay();

  return UPDATE_ALL;
}

auto CmdCyclesInfo(int nArgs) -> UpdateResult {
  if (nArgs != 1) {
    return Help_Arg_1(CMD_CYCLES_INFO);
  }
  if (strcmp(args[1].sArg, "abs") == 0) {
    video_scanner_display_info.isAbsCycle = true;
  } else if (strcmp(args[1].sArg, "rel") == 0) {
    video_scanner_display_info.isAbsCycle = false;
  } else {
    return Help_Arg_1(CMD_CYCLES_INFO);
  }

  char sText[CONSOLE_WIDTH];
  ConsoleBufferPushFormat(sText, "Cycles display updated: %s", args[1].sArg);
  ConsoleBufferToDisplay();

  return UPDATE_ALL;
}
