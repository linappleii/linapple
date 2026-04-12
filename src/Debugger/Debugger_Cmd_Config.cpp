#include "core/Common.h"
#include "Debugger_Cmd_Config.h"
#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Memory.h"
#include "Debugger_Color.h"
#include "core/Log.h"
#include "Video.h"
#include "core/Util_Text.h"
#include "Util_MemoryTextFile.h"
#include <cstddef>
#include <string>

// Globals originally from Debug.cpp
bool  g_bConfigDisasmAddressView   = true;
int   g_bConfigDisasmClick         = 4; // GH#462 alt=1, ctrl=2, shift=4 bitmask (default to Shift-Click)
bool  g_bConfigDisasmAddressColon  = true;
bool  g_bConfigDisasmOpcodesView   = true;
bool  g_bConfigDisasmOpcodeSpaces  = true;
int   g_iConfigDisasmTargets       = DISASM_TARGET_BOTH;
int   g_iConfigDisasmBranchType    = DISASM_BRANCH_FANCY;
int   g_bConfigDisasmImmediateChar = DISASM_IMMED_BOTH;
int   g_iConfigDisasmScroll        = 3; // favor 3 byte opcodes
bool  g_bConfigInfoTargetPointer   = false;

MemoryTextFile_t g_ConfigState;

bool g_bReportMissingScripts = true;

std::string g_sFileNameConfig = "LinAppleDebugger.cfg";
extern bool g_bBenchmarking;
extern bool g_bProfiling;

// Externs for globals defined elsewhere
extern int g_nDisasmDisplayLines;
extern unsigned short g_nDisasmCurAddress;
extern int g_nDisasmCurLine;
extern FontConfig_t g_aFontConfig[ NUM_FONTS ];
extern int g_iFontSpacing;
extern int g_nProfileLine;
extern const std::string g_FileNameProfile;

extern int g_iColorScheme;

// Local prototypes
void WindowUpdateSizes();
int GetConsoleTopPixels(int nConsoleDisplayLines);
void CpuSetupBenchmark();
void ProfileReset();
void ProfileFormat( bool bExport, int iFormat );
char* ProfileLinePeek( int iLine );
bool ProfileSave();

// Implementation

//===========================================================================
Update_t CmdConfigColorMono (int nArgs)
{
  int iScheme = 0;

  if (g_iCommand == CMD_CONFIG_COLOR)
    iScheme = SCHEME_COLOR;
  if (g_iCommand == CMD_CONFIG_MONOCHROME)
    iScheme = SCHEME_MONO;
  if (g_iCommand == CMD_CONFIG_BW)
    iScheme = SCHEME_BW;

  if ((iScheme < 0) || (iScheme > NUM_COLOR_SCHEMES)) // sanity check
    iScheme = SCHEME_COLOR;

  if (! nArgs)
  {
    g_iColorScheme = iScheme;
    UpdateDisplay( UPDATE_BACKGROUND );
    return UPDATE_ALL;
  }

//  if ((nArgs != 1) && (nArgs != 4))
  if (nArgs > 4)
    return HelpLastCommand();

  int iColor = g_aArgs[ 1 ].nValue;
  if ((iColor < 0) || iColor >= NUM_DEBUG_COLORS)
    return HelpLastCommand();

  int iParam;
  int nFound = FindParam( g_aArgs[ 1 ].sArg, MATCH_EXACT, iParam, _PARAM_GENERAL_BEGIN, _PARAM_GENERAL_END );

  if (nFound)
  {
    if (iParam == PARAM_RESET)
    {
      ConfigColorsReset();
      ConsoleBufferPush( " Resetting colors."  );
    }
    else
    if (iParam == PARAM_SAVE)
    {
    }
    else
    if (iParam == PARAM_LOAD)
    {
    }
    else
      return HelpLastCommand();
  }
  else
  {
    if (nArgs == 1)
    {  // Dump Color
      _CmdColorGet( iScheme, iColor );
      return ConsoleUpdate();
    }
    else
    if (nArgs == 4)
    {  // Set Color
      int R = g_aArgs[2].nValue & 0xFF;
      int G = g_aArgs[3].nValue & 0xFF;
      int B = g_aArgs[4].nValue & 0xFF;
      unsigned int nColor = RGB(R,G,B);

      DebuggerSetColor( iScheme, iColor, nColor );
    }
    else
      return HelpLastCommand();
  }

  return UPDATE_ALL;
}

Update_t CmdConfigHColor (int nArgs)
{
  if ((nArgs != 1) && (nArgs != 4))
    return Help_Arg_1( g_iCommand );

  int iColor = g_aArgs[ 1 ].nValue;
  if ((iColor < 0) || iColor >= NUM_DEBUG_COLORS)
    return Help_Arg_1( g_iCommand );

  if (nArgs == 1)
  {  // Dump Color
// TODO/FIXME: must export AW_Video.cpp: static LPBITMAPINFO  framebufferinfo;
//    unsigned int nColor = g_aColors[ iScheme ][ iColor ];
//    _ColorPrint( iColor, nColor );
    return ConsoleUpdate();
  }
  else
  {  // Set Color
    return UPDATE_ALL;
  }
}

//===========================================================================
Update_t CmdConfigLoad (int nArgs)
{
  // TODO: CmdConfigRun( gaFileNameConfig )

//  char sFileNameConfig[ MAX_PATH ];
  if (! nArgs)
  {

  }

//  gDebugConfigName
  // DEBUGLOAD file // load debugger setting
  return UPDATE_ALL;
}


//===========================================================================
bool ConfigSave_BufferToDisk ( const char *pFileName, ConfigSave_t eConfigSave )
{
  bool bStatus = false;

  const char sModeCreate[] = "w+t";
  const char sModeAppend[] = "a+t";
  const char *pMode = NULL;
  if (eConfigSave == CONFIG_SAVE_FILE_CREATE)
    pMode = sModeCreate;
  else
  if (eConfigSave == CONFIG_SAVE_FILE_APPEND)
    pMode = sModeAppend;

  std::string sFileName = g_state.sCurrentDir;
  sFileName += pFileName; // TODO: g_sDebugDir

  FILE *hFile = fopen( pFileName, pMode );

  if (hFile)
  {
    char *pText;
    int   nLine = g_ConfigState.GetNumLines();
    int   iLine;

    for( iLine = 0; iLine < nLine; iLine++ )
    {
      pText = g_ConfigState.GetLine( iLine );
      if ( pText )
      {
        fputs( pText, hFile );
      }
    }

    fclose( hFile );
    bStatus = true;
  }
  else
  {
  }

  return bStatus;
}


//===========================================================================
void ConfigSave_PrepareHeader ( const Parameters_e eCategory, const Commands_e eCommandClear )
{
  char sText[ CONSOLE_WIDTH ];

  sprintf( sText, "%s %s = %s\n"
    , g_aTokens[ TOKEN_COMMENT_EOL  ].sToken
    , g_aParameters[ PARAM_CATEGORY ].m_sName
    , g_aParameters[ eCategory ].m_sName
    );
  g_ConfigState.PushLine( sText );

  sprintf( sText, "%s %s\n"
    , g_aCommands[ eCommandClear ].m_sName
    , g_aParameters[ PARAM_WILDSTAR ].m_sName
  );
  g_ConfigState.PushLine( sText );
}


// Save Debugger Settings
//===========================================================================
Update_t CmdConfigSave (int nArgs)
{
  (void)nArgs;
  const std::string sFilename = g_state.sProgramDir + g_sFileNameConfig;

    // Bookmarks
    CmdBookmarkSave( 0 );

    // Breakpoints
    CmdBreakpointSave( 0 );

    // Watches
    CmdWatchSave( 0 );

    // Zeropage pointers
    CmdZeroPageSave( 0 );

    // Color Palete

    // Color Index
    // CmdColorSave( 0 );

    // UserSymbol

    // History

  return UPDATE_CONSOLE_DISPLAY;
}


// Config - Disasm ________________________________________________________________________________

Update_t CmdConfigDisasm( int nArgs )
{
  int iParam = 0;
  char sText[ CONSOLE_WIDTH ];

  bool bDisplayCurrentSettings = false;

//  if (! strcmp( g_aArgs[ 1 ].sArg, g_aParameters[ PARAM_WILDSTAR ].m_sName ))
  if (! nArgs)
  {
    bDisplayCurrentSettings = true;
    nArgs = PARAM_CONFIG_NUM;
  }
  else
  {
    if (nArgs > 2)
      return Help_Arg_1( CMD_CONFIG_DISASM );
  }

  for (int iArg = 1; iArg <= nArgs; iArg++ )
  {
    if (bDisplayCurrentSettings)
      iParam = _PARAM_CONFIG_BEGIN + iArg - 1;
    else
    if (FindParam( g_aArgs[iArg].sArg, MATCH_FUZZY, iParam ))
    {
    }

      switch (iParam)
      {
        case PARAM_CONFIG_BRANCH:
          if ((nArgs > 1) && (! bDisplayCurrentSettings)) // set
          {
            iArg++;
            g_iConfigDisasmBranchType = g_aArgs[ iArg ].nValue;
            if (g_iConfigDisasmBranchType < 0)
              g_iConfigDisasmBranchType = 0;
            if (g_iConfigDisasmBranchType >= NUM_DISASM_BRANCH_TYPES)
              g_iConfigDisasmBranchType = NUM_DISASM_BRANCH_TYPES - 1;

          }
          else // show current setting
          {
            ConsoleBufferPushFormat( sText,  "Branch Type: %d" , g_iConfigDisasmBranchType );
            ConsoleBufferToDisplay();
          }
          break;

        case PARAM_CONFIG_CLICK: // GH#462
          if ((nArgs > 1) && (! bDisplayCurrentSettings)) // set
          {
            iArg++;
            g_bConfigDisasmClick = (g_aArgs[ iArg ].nValue) & 7; // MAGIC NUMBER
          }
//          else // Always show current setting -- TODO: Fix remaining disasm to show current setting when set
          {
            const char *aClickKey[8] =
            {
               ""                 // 0
              ,"Alt "             // 1
              ,"Ctrl "            // 2
              ,"Alt+Ctrl "        // 3
              ,"Shift "           // 4
              ,"Shift+Alt "       // 5
              ,"Shift+Ctrl "      // 6
              ,"Shift+Ctarl+Alt " // 7
            };
            ConsoleBufferPushFormat( sText,  "Click: %d = %sLeft click" , g_bConfigDisasmClick, aClickKey[ g_bConfigDisasmClick & 7 ] );
            ConsoleBufferToDisplay();
          }
          break;

        case PARAM_CONFIG_COLON:
          if ((nArgs > 1) && (! bDisplayCurrentSettings)) // set
          {
            iArg++;
            g_bConfigDisasmAddressColon = (g_aArgs[ iArg ].nValue) != 0;
          }
          else // show current setting
          {
            int iState = g_bConfigDisasmAddressColon ? PARAM_ON : PARAM_OFF;
            ConsoleBufferPushFormat( sText,  "Colon: %s" , g_aParameters[ iState ].m_sName );
            ConsoleBufferToDisplay();
          }
          break;

        case PARAM_CONFIG_OPCODE:
          if ((nArgs > 1) && (! bDisplayCurrentSettings)) // set
          {
            iArg++;
            g_bConfigDisasmOpcodesView = (g_aArgs[ iArg ].nValue) != 0;
          }
          else
          {
            int iState = g_bConfigDisasmOpcodesView ? PARAM_ON : PARAM_OFF;
            ConsoleBufferPushFormat( sText,  "Opcodes: %s" , g_aParameters[ iState ].m_sName );
            ConsoleBufferToDisplay();
          }
          break;

        case PARAM_CONFIG_POINTER:
          if ((nArgs > 1) && (! bDisplayCurrentSettings)) // set
          {
            iArg++;
            g_bConfigInfoTargetPointer = (g_aArgs[ iArg ].nValue) != 0;
          }
          else
          {
            int iState = g_bConfigInfoTargetPointer ? PARAM_ON : PARAM_OFF;
            ConsoleBufferPushFormat( sText,  "Info Target Pointer: %s" , g_aParameters[ iState ].m_sName );
            ConsoleBufferToDisplay();
          }
          break;

        case PARAM_CONFIG_SPACES:
          if ((nArgs > 1) && (! bDisplayCurrentSettings)) // set
          {
            iArg++;
            g_bConfigDisasmOpcodeSpaces = (g_aArgs[ iArg ].nValue) != 0;
          }
          else
          {
            int iState = g_bConfigDisasmOpcodeSpaces ? PARAM_ON : PARAM_OFF;
            ConsoleBufferPushFormat( sText,  "Opcode spaces: %s" , g_aParameters[ iState ].m_sName );
            ConsoleBufferToDisplay();
          }
          break;

        case PARAM_CONFIG_TARGET:
          if ((nArgs > 1) && (! bDisplayCurrentSettings)) // set
          {
            iArg++;
            g_iConfigDisasmTargets = g_aArgs[ iArg ].nValue;
            if (g_iConfigDisasmTargets < 0)
              g_iConfigDisasmTargets = 0;
            if (g_iConfigDisasmTargets >= NUM_DISASM_TARGET_TYPES)
              g_iConfigDisasmTargets = NUM_DISASM_TARGET_TYPES - 1;
          }
          else // show current setting
          {
            ConsoleBufferPushFormat( sText,  "Target: %d" , g_iConfigDisasmTargets );
            ConsoleBufferToDisplay();
          }
          break;

        default:
          return Help_Arg_1( CMD_CONFIG_DISASM ); // CMD_CONFIG_DISASM_OPCODE );
      }
//    }
//    else
//      return Help_Arg_1( CMD_CONFIG_DISASM );
  }
  return UPDATE_CONSOLE_DISPLAY | UPDATE_DISASM;
}


//===========================================================================
Update_t CmdConfigFontLoad( int nArgs )
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}


//===========================================================================
Update_t CmdConfigFontSave( int nArgs )
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}


//===========================================================================
Update_t CmdConfigFontMode( int nArgs )
{
  if (nArgs != 2)
    return Help_Arg_1( CMD_CONFIG_FONT );

  int nMode = g_aArgs[ 2 ].nValue;

  if ((nMode < 0) || (nMode >= NUM_FONT_SPACING))
    return Help_Arg_1( CMD_CONFIG_FONT );

  g_iFontSpacing = nMode;
  _UpdateWindowFontHeights( g_aFontConfig[ FONT_DISASM_DEFAULT ]._nFontHeight );

  return UPDATE_CONSOLE_DISPLAY | UPDATE_DISASM;
}


//===========================================================================
Update_t CmdConfigFont (int nArgs)
{
  int iArg;

  if (! nArgs)
    return CmdConfigGetFont( nArgs );
  else
  if (nArgs <= 2) // nArgs
  {
    iArg = 1;

    // FONT * is undocumented, like VERSION *
    if ((! strcmp( g_aArgs[ iArg ].sArg, g_aParameters[ PARAM_WILDSTAR        ].m_sName )) ||
      (! strcmp( g_aArgs[ iArg ].sArg, g_aParameters[ PARAM_MEM_SEARCH_WILD ].m_sName )) )
    {
      char sText[ CONSOLE_WIDTH ];
      ConsoleBufferPushFormat( sText, "Lines: %d  Font Px: %d  Line Px: %d"
        , g_nDisasmDisplayLines
        , g_aFontConfig[ FONT_DISASM_DEFAULT ]._nFontHeight
        , g_aFontConfig[ FONT_DISASM_DEFAULT ]._nLineHeight );
      ConsoleBufferToDisplay();
      return UPDATE_CONSOLE_DISPLAY;
    }

    int iFound;
    int nFound;

    nFound = FindParam( g_aArgs[iArg].sArg, MATCH_EXACT, iFound, _PARAM_GENERAL_BEGIN, _PARAM_GENERAL_END );
    if (nFound)
    {
      switch( iFound )
      {
        case PARAM_LOAD:
          return CmdConfigFontLoad( nArgs );
          break;
        case PARAM_SAVE:
          return CmdConfigFontSave( nArgs );
          break;
        // TODO: FONT SIZE #
        // TODO: AA {ON|OFF}
        default:
          break;
      }
    }

    nFound = FindParam( g_aArgs[iArg].sArg, MATCH_EXACT, iFound, _PARAM_FONT_BEGIN, _PARAM_FONT_END );
    if (nFound)
    {
      if (iFound == PARAM_FONT_MODE)
        return CmdConfigFontMode( nArgs );
    }

    return CmdConfigSetFont( nArgs );
  }

  return Help_Arg_1( CMD_CONFIG_FONT );
}

//===========================================================================
Update_t CmdConfigSetFont (int nArgs)
{
  (void)nArgs;
#if OLD_FONT
  HFONT  hFont = (HFONT) 0;
  char *pFontName = NULL;
  int    nHeight = g_nFontHeight;
  int    iFontTarget = FONT_DISASM_DEFAULT;
  int    iFontPitch  = FIXED_PITCH   | FF_MODERN;
//  int    iFontMode   =
  bool   bHaveTarget = false;
  bool   bHaveFont = false;

  if (! nArgs)
  { // reset to defaut font
    pFontName = g_sFontNameDefault;
  }
  else
  if (nArgs <= 3)
  {
    int iArg = 1;
    pFontName = g_aArgs[1].sArg;

    // [DISASM|INFO|CONSOLE] "FontName" [#]
    // "FontName" can be either arg 1 or 2

    int iFound;
    int nFound = FindParam( g_aArgs[iArg].sArg, MATCH_EXACT, iFound, _PARAM_WINDOW_BEGIN, _PARAM_WINDOW_END );
    if (nFound)
    {
      switch( iFound )
      {
        case PARAM_DISASM : iFontTarget = FONT_DISASM_DEFAULT; iFontPitch = FIXED_PITCH   | FF_MODERN    ; bHaveTarget = true; break;
        case PARAM_INFO   : iFontTarget = FONT_INFO          ; iFontPitch = FIXED_PITCH   | FF_MODERN    ; bHaveTarget = true; break;
        case PARAM_CONSOLE: iFontTarget = FONT_CONSOLE       ; iFontPitch = DEFAULT_PITCH | FF_DECORATIVE; bHaveTarget = true; break;
        default:
          if (g_aArgs[2].bType != TOKEN_QUOTE_DOUBLE)
            return Help_Arg_1( CMD_CONFIG_FONT );
          break;
      }
      if (bHaveTarget)
      {
        pFontName = g_aArgs[2].sArg;
      }
    }
    else
    if (nArgs == 2)
    {
      nHeight = atoi(g_aArgs[2].sArg );
      if ((nHeight < 6) || (nHeight > 36))
        nHeight = g_nFontHeight;
    }
  }
  else
  {
    return Help_Arg_1( CMD_CONFIG_FONT );
  }

  if (! _CmdConfigFont( iFontTarget, pFontName, iFontPitch, nHeight ))
  {
  }
#endif
  return UPDATE_ALL;
}

//===========================================================================
Update_t CmdConfigGetFont (int nArgs)
{
  if (! nArgs)
  {
    for (int iFont = 0; iFont < NUM_FONTS; iFont++ )
    {
      char sText[ CONSOLE_WIDTH ] = "";
      ConsoleBufferPushFormat( sText, "  Font: %-20s  A:%2d  M:%2d",
//        g_sFontNameCustom, g_nFontWidthAvg, g_nFontWidthMax );
        g_aFontConfig[ iFont ]._sFontName,
        g_aFontConfig[ iFont ]._nFontWidthAvg,
        g_aFontConfig[ iFont ]._nFontWidthMax );
    }
    return ConsoleUpdate();
  }

  return UPDATE_CONSOLE_DISPLAY;
}

// Only for FONT_DISASM_DEFAULT !
//===========================================================================
void _UpdateWindowFontHeights( int nFontHeight )
{
  if (nFontHeight)
  {
    int nConsoleTopY = GetConsoleTopPixels( g_nConsoleDisplayLines );

    int nHeight = 0;

    if (g_iFontSpacing == FONT_SPACING_CLASSIC)
    {
      nHeight = nFontHeight + 1;
      g_nDisasmDisplayLines = nConsoleTopY / nHeight;
    }
    else
    if (g_iFontSpacing == FONT_SPACING_CLEAN)
    {
      nHeight = nFontHeight;
      g_nDisasmDisplayLines = nConsoleTopY / nHeight;
    }
    else
    if (g_iFontSpacing == FONT_SPACING_COMPRESSED)
    {
      nHeight = nFontHeight - 1;
      g_nDisasmDisplayLines = (nConsoleTopY + nHeight) / nHeight; // Ceil()
    }

    g_aFontConfig[ FONT_DISASM_DEFAULT ]._nLineHeight = nHeight;

//    int nHeightOptimal = (nHeight0 + nHeight1) / 2;
//    int nLinesOptimal = nConsoleTopY / nHeightOptimal;
//    g_nDisasmDisplayLines = nLinesOptimal;

    WindowUpdateSizes();
  }
}
