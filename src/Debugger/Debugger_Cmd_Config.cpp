#include "Debugger_Cmd_Config.h"

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
#include "LinAppleCore.h"
#include "Util_MemoryTextFile.h"
#include "Video.h"

// Globals originally from Debug.cpp
bool g_config_disasm_address_view = true;
int g_config_disasm_click =
    4;  // GH#462 alt=1, ctrl=2, shift=4 bitmask (default to Shift-Click)
bool g_config_disasm_address_colon = true;
bool g_config_disasm_opcodes_view = true;
bool g_config_disasm_opcode_spaces = true;
int g_config_disasm_targets = DISASM_TARGET_BOTH;
int g_config_disasm_branch_type = DISASM_BRANCH_FANCY;
int g_config_disasm_immediate_char = DISASM_IMMED_BOTH;
int g_config_disasm_scroll = 3;  // favor 3 byte opcodes
bool g_config_info_target_pointer = false;

MemoryTextFile_t g_config_state;

bool g_report_missing_scripts = true;

std::string g_file_name_config = "LinAppleDebugger.cfg";
extern bool g_benchmarking;
extern bool g_profiling;

// Externs for globals defined elsewhere
extern int g_disasm_display_lines;
extern uint16_t g_disasm_cur_address;
extern int g_disasm_cur_line;
extern FontConfig_t g_font_config[NUM_FONTS];
extern int g_font_spacing;
extern int g_profile_line_count;
extern const std::string g_file_name_profile;

extern int g_color_scheme;

// Local prototypes
void WindowUpdateSizes();
auto GetConsoleTopPixels(int nConsoleDisplayLines) -> int;
void cpu_setup_benchmark();
void ProfileReset();
void ProfileFormat(bool bExport, int iFormat);
auto ProfileLinePeek(int iLine) -> char*;
auto ProfileSave() -> bool;

// Implementation

//===========================================================================
auto CmdConfigColorMono(int nArgs) -> Update_t {
  int iScheme = 0;

  if (g_command == CMD_CONFIG_COLOR) {
    iScheme = SCHEME_COLOR;
  }
  if (g_command == CMD_CONFIG_MONOCHROME) {
    iScheme = SCHEME_MONO;
  }
  if (g_command == CMD_CONFIG_BW) {
    iScheme = SCHEME_BW;
  }

  if ((iScheme < 0) || (iScheme > NUM_COLOR_SCHEMES)) {  // sanity check
    iScheme = SCHEME_COLOR;
  }

  if (!nArgs) {
    g_color_scheme = iScheme;
    UpdateDisplay(UPDATE_BACKGROUND);
    return UPDATE_ALL;
  }

  //  if ((nArgs != 1) && (nArgs != 4))
  if (nArgs > 4) {
    return HelpLastCommand();
  }

  int iColor = g_args[1].nValue;
  if ((iColor < 0) || iColor >= NUM_DEBUG_COLORS) {
    return HelpLastCommand();
  }

  int iParam = 0;
  int nFound = FindParam(g_args[1].sArg, MATCH_EXACT, iParam,
                         _PARAM_GENERAL_BEGIN, _PARAM_GENERAL_END);

  if (nFound) {
    if (iParam == PARAM_RESET) {
      ConfigColorsReset();
      ConsoleBufferPush(" Resetting colors.");
    } else if (iParam == PARAM_SAVE) {
    } else if (iParam == PARAM_LOAD) {
    } else {
      return HelpLastCommand();
    }
  } else {
    if (nArgs == 1) {  // Dump Color
      _CmdColorGet(iScheme, iColor);
      return ConsoleUpdate();
    } else if (nArgs == 4) {  // Set Color
      int R = g_args[2].nValue & 0xFF;
      int G = g_args[3].nValue & 0xFF;
      int B = g_args[4].nValue & 0xFF;
      uint32_t nColor = RGB(R, G, B);

      DebuggerSetColor(iScheme, iColor, nColor);
    } else {
      return HelpLastCommand();
    }
  }

  return UPDATE_ALL;
}

auto CmdConfigHColor(int nArgs) -> Update_t {
  if ((nArgs != 1) && (nArgs != 4)) {
    return Help_Arg_1(g_command);
  }

  int iColor = g_args[1].nValue;
  if ((iColor < 0) || iColor >= NUM_DEBUG_COLORS) {
    return Help_Arg_1(g_command);
  }

  if (nArgs == 1) {  // Dump Color
    // TODO/FIXME: must export AW_Video.cpp: static LPBITMAPINFO
    // framebufferinfo;
    //    uint32_t nColor = g_colors[ iScheme ][ iColor ];
    //    _ColorPrint( iColor, nColor );
    return ConsoleUpdate();
  } else {  // Set Color
    return UPDATE_ALL;
  }
}

//===========================================================================
auto CmdConfigLoad(int nArgs) -> Update_t {
  // TODO: CmdConfigRun( gaFileNameConfig )

  //  char sFileNameConfig[ path_max_len ];
  if (!nArgs) {
  }

  //  gDebugConfigName
  // DEBUGLOAD file // load debugger setting
  return UPDATE_ALL;
}

//===========================================================================
auto ConfigSave_BufferToDisk(const char* pFileName, ConfigSave_t eConfigSave)
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

  std::string sFileName = g_state.current_dir.data();
  sFileName += pFileName;  // TODO: g_debug_dir

  FILE* hFile = fopen(pFileName, pMode);

  if (hFile) {
    char* text = nullptr;
    int nLine = g_config_state.GetNumLines();
    int iLine = 0;

    for (iLine = 0; iLine < nLine; iLine++) {
      text = g_config_state.GetLine(iLine);
      if (text) {
        fputs(text, hFile);
      }
    }

    fclose(hFile);
    bStatus = true;
  } else {
  }

  return bStatus;
}

//===========================================================================
void ConfigSave_PrepareHeader(const Parameters_e eCategory,
                              const Commands_e eCommandClear) {
  char sText[CONSOLE_WIDTH];

  sprintf(sText, "%s %s = %s\n", g_tokens[TOKEN_COMMENT_EOL].sToken,
          g_parameters[PARAM_CATEGORY].name, g_parameters[eCategory].name);
  g_config_state.PushLine(sText);

  sprintf(sText, "%s %s\n", g_commands[eCommandClear].name,
          g_parameters[PARAM_WILDSTAR].name);
  g_config_state.PushLine(sText);
}

// Save Debugger Settings
//===========================================================================
auto CmdConfigSave(int nArgs) -> Update_t {
  (void)nArgs;
  const std::string sFilename =
      std::string(g_state.program_dir.data()) + g_file_name_config;

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

auto CmdConfigDisasm(int nArgs) -> Update_t {
  int iParam = 0;
  char sText[CONSOLE_WIDTH];

  bool bDisplayCurrentSettings = false;

  //  if (! strcmp( g_args[ 1 ].sArg, g_parameters[ PARAM_WILDSTAR ].m_sName ))
  if (!nArgs) {
    bDisplayCurrentSettings = true;
    nArgs = PARAM_CONFIG_NUM;
  } else {
    if (nArgs > 2) {
      return Help_Arg_1(CMD_CONFIG_DISASM);
    }
  }

  for (int iArg = 1; iArg <= nArgs; iArg++) {
    if (bDisplayCurrentSettings) {
      iParam = _PARAM_CONFIG_BEGIN + iArg - 1;
    } else if (FindParam(g_args[iArg].sArg, MATCH_FUZZY, iParam)) {
    }

    switch (iParam) {
      case PARAM_CONFIG_BRANCH:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          g_config_disasm_branch_type = g_args[iArg].nValue;
          if (g_config_disasm_branch_type < 0) {
            g_config_disasm_branch_type = 0;
          }
          if (g_config_disasm_branch_type >= NUM_DISASM_BRANCH_TYPES) {
            g_config_disasm_branch_type = NUM_DISASM_BRANCH_TYPES - 1;
          }

        } else  // show current setting
        {
          ConsoleBufferPushFormat(sText, "Branch Type: %d",
                                  g_config_disasm_branch_type);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_CLICK:                          // GH#462
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          g_config_disasm_click = (g_args[iArg].nValue) & 7;  // MAGIC NUMBER
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
              "Shift+Ctarl+Alt "  // 7
          };
          ConsoleBufferPushFormat(sText, "Click: %d = %sLeft click",
                                  g_config_disasm_click,
                                  aClickKey[g_config_disasm_click & 7]);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_COLON:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          g_config_disasm_address_colon = (g_args[iArg].nValue) != 0;
        } else  // show current setting
        {
          int iState = g_config_disasm_address_colon ? PARAM_ON : PARAM_OFF;
          ConsoleBufferPushFormat(sText, "Colon: %s",
                                  g_parameters[iState].name);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_OPCODE:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          g_config_disasm_opcodes_view = (g_args[iArg].nValue) != 0;
        } else {
          int iState = g_config_disasm_opcodes_view ? PARAM_ON : PARAM_OFF;
          ConsoleBufferPushFormat(sText, "Opcodes: %s",
                                  g_parameters[iState].name);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_POINTER:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          g_config_info_target_pointer = (g_args[iArg].nValue) != 0;
        } else {
          int iState = g_config_info_target_pointer ? PARAM_ON : PARAM_OFF;
          ConsoleBufferPushFormat(sText, "info Target Pointer: %s",
                                  g_parameters[iState].name);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_SPACES:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          g_config_disasm_opcode_spaces = (g_args[iArg].nValue) != 0;
        } else {
          int iState = g_config_disasm_opcode_spaces ? PARAM_ON : PARAM_OFF;
          ConsoleBufferPushFormat(sText, "Opcode spaces: %s",
                                  g_parameters[iState].name);
          ConsoleBufferToDisplay();
        }
        break;

      case PARAM_CONFIG_TARGET:
        if ((nArgs > 1) && (!bDisplayCurrentSettings))  // set
        {
          iArg++;
          g_config_disasm_targets = g_args[iArg].nValue;
          if (g_config_disasm_targets < 0) {
            g_config_disasm_targets = 0;
          }
          if (g_config_disasm_targets >= NUM_DISASM_TARGET_TYPES) {
            g_config_disasm_targets = NUM_DISASM_TARGET_TYPES - 1;
          }
        } else  // show current setting
        {
          ConsoleBufferPushFormat(sText, "Target: %d", g_config_disasm_targets);
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
auto CmdConfigFontLoad(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdConfigFontSave(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdConfigFontMode(int nArgs) -> Update_t {
  if (nArgs != 2) {
    return Help_Arg_1(CMD_CONFIG_FONT);
  }

  int nMode = g_args[2].nValue;

  if ((nMode < 0) || (nMode >= NUM_FONT_SPACING)) {
    return Help_Arg_1(CMD_CONFIG_FONT);
  }

  g_font_spacing = nMode;
  _UpdateWindowFontHeights(g_font_config[FONT_DISASM_DEFAULT]._nFontHeight);

  return UPDATE_CONSOLE_DISPLAY | UPDATE_DISASM;
}

//===========================================================================
auto CmdConfigFont(int nArgs) -> Update_t {
  int iArg = 0;

  if (!nArgs) {
    return CmdConfigGetFont(nArgs);
  } else if (nArgs <= 2)  // nArgs
  {
    iArg = 1;

    // FONT * is undocumented, like VERSION *
    if ((!strcmp(g_args[iArg].sArg, g_parameters[PARAM_WILDSTAR].name)) ||
        (!strcmp(g_args[iArg].sArg,
                 g_parameters[PARAM_MEM_SEARCH_WILD].name))) {
      char sText[CONSOLE_WIDTH];
      ConsoleBufferPushFormat(sText, "Lines: %d  Font Px: %d  Line Px: %d",
                              g_disasm_display_lines,
                              g_font_config[FONT_DISASM_DEFAULT]._nFontHeight,
                              g_font_config[FONT_DISASM_DEFAULT]._nLineHeight);
      ConsoleBufferToDisplay();
      return UPDATE_CONSOLE_DISPLAY;
    }

    int iFound = 0;
    int nFound = 0;

    nFound = FindParam(g_args[iArg].sArg, MATCH_EXACT, iFound,
                       _PARAM_GENERAL_BEGIN, _PARAM_GENERAL_END);
    if (nFound) {
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

    nFound = FindParam(g_args[iArg].sArg, MATCH_EXACT, iFound,
                       _PARAM_FONT_BEGIN, _PARAM_FONT_END);
    if (nFound) {
      if (iFound == PARAM_FONT_MODE) {
        return CmdConfigFontMode(nArgs);
      }
    }

    return CmdConfigSetFont(nArgs);
  }

  return Help_Arg_1(CMD_CONFIG_FONT);
}

//===========================================================================
auto CmdConfigSetFont(int nArgs) -> Update_t {
  (void)nArgs;
#if OLD_FONT
  HFONT hFont = (HFONT)0;
  char* pFontName = nullptr;
  int nHeight = g_font_height;
  int iFontTarget = FONT_DISASM_DEFAULT;
  int iFontPitch = FIXED_PITCH | FF_MODERN;
  //  int    iFontMode   =
  bool bHaveTarget = false;
  bool bHaveFont = false;

  if (!nArgs) {  // reset to defaut font
    pFontName = g_font_name_default;
  } else if (nArgs <= 3) {
    int iArg = 1;
    pFontName = g_args[1].sArg;

    // [DISASM|INFO|CONSOLE] "FontName" [#]
    // "FontName" can be either arg 1 or 2

    int iFound;
    int nFound = FindParam(g_args[iArg].sArg, MATCH_EXACT, iFound,
                           _PARAM_WINDOW_BEGIN, _PARAM_WINDOW_END);
    if (nFound) {
      switch (iFound) {
        case PARAM_DISASM:
          iFontTarget = FONT_DISASM_DEFAULT;
          iFontPitch = FIXED_PITCH | FF_MODERN;
          bHaveTarget = true;
          break;
        case PARAM_INFO:
          iFontTarget = FONT_INFO;
          iFontPitch = FIXED_PITCH | FF_MODERN;
          bHaveTarget = true;
          break;
        case PARAM_CONSOLE:
          iFontTarget = FONT_CONSOLE;
          iFontPitch = DEFAULT_PITCH | FF_DECORATIVE;
          bHaveTarget = true;
          break;
        default:
          if (g_args[2].bType != TOKEN_QUOTE_DOUBLE)
            return Help_Arg_1(CMD_CONFIG_FONT);
          break;
      }
      if (bHaveTarget) {
        pFontName = g_args[2].sArg;
      }
    } else if (nArgs == 2) {
      nHeight = atoi(g_args[2].sArg);
      if ((nHeight < 6) || (nHeight > 36)) nHeight = g_font_height;
    }
  } else {
    return Help_Arg_1(CMD_CONFIG_FONT);
  }

  if (!_CmdConfigFont(iFontTarget, pFontName, iFontPitch, nHeight)) {
  }
#endif
  return UPDATE_ALL;
}

//===========================================================================
auto CmdConfigGetFont(int nArgs) -> Update_t {
  if (!nArgs) {
    for (auto& iFont : g_font_config) {
      char sText[CONSOLE_WIDTH] = "";
      ConsoleBufferPushFormat(
          sText, "  Font: %-20s  A:%2d  M:%2d",
          //        g_font_name_custom, g_font_width_avg, g_font_width_max );
          iFont._sFontName, iFont._nFontWidthAvg, iFont._nFontWidthMax);
    }
    return ConsoleUpdate();
  }

  return UPDATE_CONSOLE_DISPLAY;
}

// Only for FONT_DISASM_DEFAULT !
//===========================================================================
void _UpdateWindowFontHeights(int nFontHeight) {
  if (nFontHeight) {
    int nConsoleTopY = GetConsoleTopPixels(g_console_display_lines);

    int nHeight = 0;

    if (g_font_spacing == FONT_SPACING_CLASSIC) {
      nHeight = nFontHeight + 1;
      g_disasm_display_lines = nConsoleTopY / nHeight;
    } else if (g_font_spacing == FONT_SPACING_CLEAN) {
      nHeight = nFontHeight;
      g_disasm_display_lines = nConsoleTopY / nHeight;
    } else if (g_font_spacing == FONT_SPACING_COMPRESSED) {
      nHeight = nFontHeight - 1;
      g_disasm_display_lines = (nConsoleTopY + nHeight) / nHeight;  // Ceil()
    }

    g_font_config[FONT_DISASM_DEFAULT]._nLineHeight = nHeight;

    //    int nHeightOptimal = (nHeight0 + nHeight1) / 2;
    //    int nLinesOptimal = nConsoleTopY / nHeightOptimal;
    //    g_disasm_display_lines = nLinesOptimal;

    WindowUpdateSizes();
  }
}
