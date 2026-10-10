// SPDX-License-Identifier: GPL-2.0-only
#include "Debugger_Cmd_Config.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "Debug.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Color.h"
#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Help.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

// Globals originally from Debug.cpp
bool config_disasm_address_view = true;
int config_disasm_click =
    4;  // GH#462 alt=1, ctrl=2, shift=4 bitmask (default to Shift-Click)
bool config_disasm_address_colon = true;
bool config_disasm_opcodes_view = true;
bool config_disasm_opcode_spaces = true;
int config_disasm_targets = DISASM_TARGET_BOTH;
int config_disasm_branch_type = DISASM_BRANCH_FANCY;
int config_disasm_immediate_char = DISASM_IMMED_BOTH;
bool config_info_target_pointer = false;

MemoryTextFile config_state;

bool report_missing_scripts = true;

const char* const file_name_config = "LinAppleDebugger.cfg";

static int disasm_display_lines = 0;

// Local prototypes

// Implementation

//===========================================================================
auto CmdConfigColorMono(int nArgs) -> UpdateResult {
  int iScheme = 0;

  if (command == CMD_CONFIG_COLOR) {
    iScheme = SCHEME_COLOR;
  }
  if (command == CMD_CONFIG_MONOCHROME) {
    iScheme = SCHEME_MONO;
  }
  if (command == CMD_CONFIG_BW) {
    iScheme = SCHEME_BW;
  }

  if ((iScheme < 0) || (iScheme > NUM_COLOR_SCHEMES)) {  // sanity check
    iScheme = SCHEME_COLOR;
  }

  if (nArgs == 0) {
    color_scheme = iScheme;
    UpdateDisplay(UPDATE_BACKGROUND);
    return UPDATE_ALL;
  }

  //  if ((nArgs != 1) && (nArgs != 4))
  if (nArgs > 4) {
    return HelpLastCommand();
  }

  int iColor = args[1].nValue;
  if ((iColor < 0) || iColor >= NUM_DEBUG_COLORS) {
    return HelpLastCommand();
  }

  int iParam = 0;
  int nFound = FindParam(args[1].sArg, MATCH_EXACT, iParam,
                         PARAM_GENERAL_BEGIN, PARAM_GENERAL_END);

  if (nFound != 0) {
    if (iParam == PARAM_RESET) {
      ConfigColorsReset();
      ConsoleBufferPush(" Resetting colors.");
    } else if (iParam == PARAM_SAVE || iParam == PARAM_LOAD) {
    } else {
      return HelpLastCommand();
    }
  } else {
    if (nArgs == 1) {  // Dump Color
      CmdColorGet(iScheme, iColor);
      return ConsoleUpdate();
    }
    if (nArgs == 4) {  // Set Color
      int R = args[2].nValue & 0xFF;
      int G = args[3].nValue & 0xFF;
      int B = args[4].nValue & 0xFF;
      uint32_t nColor = RGB(R, G, B);

      DebuggerSetColor(iScheme, iColor, nColor);
    } else {
      return HelpLastCommand();
    }
  }

  return UPDATE_ALL;
}

auto CmdConfigHColor(int nArgs) -> UpdateResult {
  if ((nArgs != 1) && (nArgs != 4)) {
    return Help_Arg_1(command);
  }

  int iColor = args[1].nValue;
  if ((iColor < 0) || iColor >= NUM_DEBUG_COLORS) {
    return Help_Arg_1(command);
  }

  if (nArgs == 1) {  // Dump Color
    // TODO/FIXME: must export AW_Video.cpp: static LPBITMAPINFO
    // framebufferinfo;
    //    uint32_t nColor = colors[ iScheme ][ iColor ];
    //    ColorPrint( iColor, nColor );
    return ConsoleUpdate();
  }  // Set Color
  return UPDATE_ALL;
}

//===========================================================================
auto CmdConfigLoad(int nArgs) -> UpdateResult {
  // TODO: CmdConfigRun( gaFileNameConfig )

  //  char sFileNameConfig[ path_max_len ];
  if (nArgs == 0) {
  }

  //  gDebugConfigName
  // DEBUGLOAD file // load debugger setting
  return UPDATE_ALL;
}

//===========================================================================
auto ConfigSave_BufferToDisk(const char* pFileName, ConfigSave eConfigSave)
    -> bool {
  bool bStatus = false;

  const char sModeCreate[] = "w+t";
  const char sModeAppend[] = "a+t";
  const char* pMode = nullptr;
  if (eConfigSave == CONFIG_SAVE_FILE_CREATE) {
    pMode = sModeCreate;
  } else if (eConfigSave == CONFIG_SAVE_FILE_APPEND) {
    pMode = sModeAppend;
  }

  std::string sFileName = system_state.current_dir.data();
  sFileName += pFileName;  // TODO: debug_dir

  FilePtr file{fopen(pFileName, pMode), fclose};

  if (file) {
    char* text = nullptr;
    int num_lines = config_state.GetNumLines();

    for (int line_idx = 0; line_idx < num_lines; line_idx++) {
      text = config_state.GetLine(line_idx);
      if (text != nullptr) {
        fputs(text, file.get());
      }
    }
    bStatus = true;
  }

  return bStatus;
}

//===========================================================================
auto ConfigSave_PrepareHeader(const Parameters eCategory,
                              const Commands eCommandClear) -> void {
  char sText[CONSOLE_WIDTH];

  snprintf(sText, sizeof(sText), "%s %s = %s\n",
           tokens[TOKEN_COMMENT_EOL].sToken,
           parameters[PARAM_CATEGORY].name, parameters[eCategory].name);
  config_state.PushLine(sText);

  snprintf(sText, sizeof(sText), "%s %s\n", commands[eCommandClear].name,
           parameters[PARAM_WILDSTAR].name);
  config_state.PushLine(sText);
}

// Save Debugger Settings
//===========================================================================
auto CmdConfigSave(int nArgs) -> UpdateResult {
  (void)nArgs;

  // Bookmarks
  CmdBookmarkSave(0);

  // Breakpoints
  CmdBreakpointSave(0);

  // Watches
  CmdWatchSave(0);

  // Zeropage pointers
  CmdZeroPageSave(0);

  // Color Palete

  // Color Index
  // CmdColorSave( 0 );

  // UserSymbol

  // History

  return UPDATE_CONSOLE_DISPLAY;
}

// Config - Disasm
// ________________________________________________________________________________

auto CmdConfigDisasm(int nArgs) -> UpdateResult {
  int iParam = 0;
  char sText[CONSOLE_WIDTH];

  bool bDisplayCurrentSettings = false;

  //  if (! strcmp( args[ 1 ].sArg, parameters[ PARAM_WILDSTAR ].name ))
  if (nArgs == 0) {
    bDisplayCurrentSettings = true;
    nArgs = PARAM_CONFIG_NUM;
  } else {
    if (nArgs > 2) {
      return Help_Arg_1(CMD_CONFIG_DISASM);
    }
  }

  for (int iArg = 1; iArg <= nArgs; iArg++) {
    if (bDisplayCurrentSettings) {
      iParam = PARAM_CONFIG_BEGIN + iArg - 1;
    } else if (FindParam(args[iArg].sArg, MATCH_FUZZY, iParam) != 0) {
    }

    switch (iParam) {
      case PARAM_CONFIG_BRANCH:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          config_disasm_branch_type = args[iArg].nValue;
          config_disasm_branch_type =
              std::max(config_disasm_branch_type, 0);
          if (config_disasm_branch_type >= NUM_DISASM_BRANCH_TYPES) {
            config_disasm_branch_type = NUM_DISASM_BRANCH_TYPES - 1;
          }

        } else  // show current setting
        {
          ConsoleBufferPushFormat(sText, "Branch Type: %d",
                                  config_disasm_branch_type);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_CLICK:                          // GH#462
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          config_disasm_click = args[iArg].nValue & 7;  // MAGIC NUMBER
        }
        //          else // Always show current setting -- TODO: Fix remaining
        //          disasm to show current setting when set
        {
          const char* aClickKey[8] = {
              ""  // 0
              ,
              "Alt "  // 1
              ,
              "Ctrl "  // 2
              ,
              "Alt+Ctrl "  // 3
              ,
              "Shift "  // 4
              ,
              "Shift+Alt "  // 5
              ,
              "Shift+Ctrl "  // 6
              ,
              "Shift+Ctarl+Alt ",  // 7
          };
          ConsoleBufferPushFormat(sText, "Click: %d = %sLeft click",
                                  config_disasm_click,
                                  aClickKey[config_disasm_click & 7]);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_COLON:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          config_disasm_address_colon = args[iArg].nValue != 0;
        } else  // show current setting
        {
          int iState = config_disasm_address_colon ? PARAM_ON : PARAM_OFF;
          ConsoleBufferPushFormat(sText, "Colon: %s",
                                  parameters[iState].name);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_OPCODE:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          config_disasm_opcodes_view = args[iArg].nValue != 0;
        } else {
          int iState = config_disasm_opcodes_view ? PARAM_ON : PARAM_OFF;
          ConsoleBufferPushFormat(sText, "Opcodes: %s",
                                  parameters[iState].name);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_POINTER:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          config_info_target_pointer = args[iArg].nValue != 0;
        } else {
          int iState = config_info_target_pointer ? PARAM_ON : PARAM_OFF;
          ConsoleBufferPushFormat(sText, "info Target Pointer: %s",
                                  parameters[iState].name);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_SPACES:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          config_disasm_opcode_spaces = args[iArg].nValue != 0;
        } else {
          int iState = config_disasm_opcode_spaces ? PARAM_ON : PARAM_OFF;
          ConsoleBufferPushFormat(sText, "Opcode spaces: %s",
                                  parameters[iState].name);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_TARGET:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          config_disasm_targets = args[iArg].nValue;
          config_disasm_targets = std::max(config_disasm_targets, 0);
          if (config_disasm_targets >= NUM_DISASM_TARGET_TYPES) {
            config_disasm_targets = NUM_DISASM_TARGET_TYPES - 1;
          }
        } else  // show current setting
        {
          ConsoleBufferPushFormat(sText, "Target: %d", config_disasm_targets);
          ConsoleBufferToDisplay();
        }
        break;

      default:
        return Help_Arg_1(CMD_CONFIG_DISASM);  // CMD_CONFIG_DISASM_OPCODE );
    }
    //    }
    //    else
    //      return Help_Arg_1( CMD_CONFIG_DISASM );
  }
  return UPDATE_CONSOLE_DISPLAY | UPDATE_DISASM;
}

//===========================================================================
auto CmdConfigFontLoad(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdConfigFontSave(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdConfigFontMode(int nArgs) -> UpdateResult {
  if (nArgs != 2) {
    return Help_Arg_1(CMD_CONFIG_FONT);
  }

  int nMode = args[2].nValue;

  if ((nMode < 0) || (nMode >= NUM_FONT_SPACING)) {
    return Help_Arg_1(CMD_CONFIG_FONT);
  }

  font_spacing = nMode;
  UpdateWindowFontHeights(font_config[FONT_DISASM_DEFAULT].font_height);

  return UPDATE_CONSOLE_DISPLAY | UPDATE_DISASM;
}

//===========================================================================
auto CmdConfigFont(int nArgs) -> UpdateResult {
  int iArg = 0;

  if (nArgs == 0) {
    return CmdConfigGetFont(nArgs);
  }
  if (nArgs <= 2)  // nArgs
  {
    iArg = 1;

    // FONT * is undocumented, like VERSION *
    if ((strcmp(args[iArg].sArg, parameters[PARAM_WILDSTAR].name) == 0) ||
        (strcmp(args[iArg].sArg, parameters[PARAM_MEM_SEARCH_WILD].name) ==
         0)) {
      char sText[CONSOLE_WIDTH];
      ConsoleBufferPushFormat(sText, "Lines: %d  Font Px: %d  Line Px: %d",
                              disasm_display_lines,
                              font_config[FONT_DISASM_DEFAULT].font_height,
                              font_config[FONT_DISASM_DEFAULT].line_height);
      ConsoleBufferToDisplay();
      return UPDATE_CONSOLE_DISPLAY;
    }

    int iFound = 0;
    int nFound = 0;

    nFound = FindParam(args[iArg].sArg, MATCH_EXACT, iFound,
                       PARAM_GENERAL_BEGIN, PARAM_GENERAL_END);
    if (nFound != 0) {
      switch (iFound) {
        case PARAM_LOAD:
          return CmdConfigFontLoad(nArgs);
          break;
        case PARAM_SAVE:
          return CmdConfigFontSave(nArgs);
          break;
        // TODO: FONT SIZE #
        // TODO: AA {ON|OFF}
        default:
          break;
      }
    }

    nFound = FindParam(args[iArg].sArg, MATCH_EXACT, iFound, PARAM_FONT_BEGIN,
                       PARAM_FONT_END);
    if ((nFound != 0) && (iFound == PARAM_FONT_MODE)) {
      return CmdConfigFontMode(nArgs);
    }

    return CmdConfigSetFont(nArgs);
  }

  return Help_Arg_1(CMD_CONFIG_FONT);
}

//===========================================================================
auto CmdConfigSetFont(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_ALL;
}

//===========================================================================
auto CmdConfigGetFont(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    for (auto& iFont : font_config) {
      char sText[CONSOLE_WIDTH] = "";
      ConsoleBufferPushFormat(
          sText, "  Font: %-20s  A:%2d  M:%2d",
          //        font_name_custom, font_width_avg, font_width_max );
          iFont.font_name, iFont.font_width_avg, iFont.font_width_max);
    }
    return ConsoleUpdate();
  }

  return UPDATE_CONSOLE_DISPLAY;
}

// Only for FONT_DISASM_DEFAULT !
//===========================================================================
auto UpdateWindowFontHeights(int nFontHeight) -> void {
  if (nFontHeight != 0) {
    int nConsoleTopY = GetConsoleTopPixels(console_display_lines);

    int nHeight = 0;

    if (font_spacing == FONT_SPACING_CLASSIC) {
      nHeight = nFontHeight + 1;
      disasm_display_lines = nConsoleTopY / nHeight;
    } else if (font_spacing == FONT_SPACING_CLEAN) {
      nHeight = nFontHeight;
      disasm_display_lines = nConsoleTopY / nHeight;
    } else if (font_spacing == FONT_SPACING_COMPRESSED) {
      nHeight = nFontHeight - 1;
      disasm_display_lines = (nConsoleTopY + nHeight) / nHeight;  // Ceil()
    }

    font_config[FONT_DISASM_DEFAULT].line_height = nHeight;

    //    int nHeightOptimal = (nHeight0 + nHeight1) / 2;
    //    int nLinesOptimal = nConsoleTopY / nHeightOptimal;
    //    disasm_display_lines = nLinesOptimal;

    WindowUpdateSizes();
  }
}
