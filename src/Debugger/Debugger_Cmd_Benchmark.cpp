#include "Common.h"
#include <algorithm>
#include <vector>
#include <string>
#include "Debugger_Cmd_Benchmark.h"
#include "Debugger_Cmd_CPU.h"
#include "Debugger_Assembler.h"
#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Debugger_Color.h"
#include "Log.h"
#include "Video.h"
#include "Frame.h"
#include "CPU.h"
#include "Util_Text.h"
#include "Keyboard.h"
#include "SDL3/SDL.h"

// Globals originally from Debug.cpp
bool g_bBenchmarking = false;
bool g_bProfiling = false;

ProfileOpcode_t g_aProfileOpcodes[ NUM_OPCODES ];
ProfileOpmode_t g_aProfileOpmodes[ NUM_OPMODES ];
uint64_t g_nProfileBeginCycles = 0; // g_nCumulativeCycles // PROFILE RESET

const std::string g_FileNameProfile = "Profile.txt";
int   g_nProfileLine = 0;
char  g_aProfileLine[ NUM_PROFILE_LINES ][ CONSOLE_WIDTH ];

unsigned int extbench = 0;

// Externs
extern unsigned short g_nDisasmCurAddress;
extern uint64_t g_nCumulativeCycles;

// Implementation

//===========================================================================
Update_t CmdBenchmark (int nArgs)
{
  (void)nArgs;
  if (g_bBenchmarking)
    CmdBenchmarkStart(0);
  else
    CmdBenchmarkStop(0);

  return UPDATE_ALL; // TODO/FIXME Verify
}

Update_t CmdBenchmarkStart (int nArgs)
{
  (void)nArgs;
  CpuSetupBenchmark();
  g_nDisasmCurAddress = regs.pc;
  DisasmCalcTopBotAddress();
  g_bBenchmarking = true;
  return UPDATE_ALL; // 1;
}

Update_t CmdBenchmarkStop (int nArgs)
{
  (void)nArgs;
  g_bBenchmarking = false;
  DebugEnd();

  FrameRefreshStatus(DRAW_TITLE);
  VideoRedrawScreen();
  unsigned int currtime = SDL_GetTicks();
  while ((extbench = SDL_GetTicks()) != currtime)
    ; // intentional busy-waiting
  KeybQueueKeypress(' ');

  return UPDATE_ALL; // 0;
}

Update_t CmdProfile (int nArgs)
{
  if (! nArgs)
  {
    sprintf( g_aArgs[ 1 ].sArg, "%s", g_aParameters[ PARAM_RESET ].m_sName );
    nArgs = 1;
  }

  if (nArgs == 1)
  {
    int iParam;
    int nFound = FindParam( g_aArgs[ 1 ].sArg, MATCH_EXACT, iParam, _PARAM_GENERAL_BEGIN, _PARAM_GENERAL_END );

    if (! nFound)
      goto _Help;

    if (iParam == PARAM_RESET)
    {
      ProfileReset();
      g_bProfiling = true;
      ConsoleBufferPush( " Resetting profile data."  );
    }
    else
    {
      if ((iParam != PARAM_SAVE) && (iParam != PARAM_LIST))
        goto _Help;

      bool bExport = true;
      if (iParam == PARAM_LIST)
        bExport = false;

      ProfileFormat( bExport, bExport ? PROFILE_FORMAT_TAB : PROFILE_FORMAT_SPACE );

      // Dump to console
      if (iParam == PARAM_LIST)
      {
        char *pText;
        char  sText[ CONSOLE_WIDTH ];

        int   nLine = g_nProfileLine;
        int   iLine;

        for( iLine = 0; iLine < nLine; iLine++ )
        {
          pText = ProfileLinePeek( iLine );
          if (pText)
          {
            TextConvertTabsToSpaces( sText, pText, CONSOLE_WIDTH, 4 );
            ConsolePrint( sText );
          }
        }
      }

      if (iParam == PARAM_SAVE)
      {
        if (ProfileSave())
        {
          char sText[ CONSOLE_WIDTH ];
          ConsoleBufferPushFormat ( sText, " Saved: %s", g_FileNameProfile.c_str() );
        }
        else
          ConsoleBufferPush( " ERROR: Couldn't save file. (In use?" );
      }
    }
  }
  else
    goto _Help;

  return ConsoleUpdate(); // UPDATE_CONSOLE_DISPLAY;

_Help:
  return Help_Arg_1( CMD_PROFILE );
}

char * ProfileLinePeek ( int iLine )
{
  char *pText = NULL;

  if (iLine < 0)
    iLine = 0;

  if (iLine <= g_nProfileLine)
    pText = & g_aProfileLine[ iLine ][ 0 ];

  return pText;
}

char * ProfileLinePush ()
{
  if (g_nProfileLine < NUM_PROFILE_LINES)
  {
    g_nProfileLine++;
  }

  return ProfileLinePeek( g_nProfileLine  );
}

void ProfileLineReset()
{
  g_nProfileLine = 0;
}

#define DELIM "%s"
void ProfileFormat( bool bExport, int eFormatMode )
{
  char sSeperator7[ 32 ] = "\t";
  char sSeperator2[ 32 ] = "\t";
  char sSeperator1[ 32 ] = "\t";
  char sOpcode [ 8 ]; // 2 chars for opcode in hex, plus quotes on either side
  char sAddress[MAX_OPMODE_NAME+2];

  if (eFormatMode == PROFILE_FORMAT_COMMA)
  {
    sSeperator7[0] = ',';
    sSeperator2[0] = ',';
    sSeperator1[0] = ',';
  }
  else
  if (eFormatMode == PROFILE_FORMAT_SPACE)
  {
    sprintf( sSeperator7, "       " ); // 7
    sprintf( sSeperator2, "  "      ); // 2
    sprintf( sSeperator1, " "       ); // 1
  }

  ProfileLineReset();
  char *pText = ProfileLinePeek( 0 );

  int iOpcode;
  int iOpmode;

  bool bOpcodeGood = true;
  bool bOpmodeGood = true;

  std::vector< ProfileOpcode_t > vProfileOpcode( &g_aProfileOpcodes[0], &g_aProfileOpcodes[ NUM_OPCODES ] );
  std::vector< ProfileOpmode_t > vProfileOpmode( &g_aProfileOpmodes[0], &g_aProfileOpmodes[ NUM_OPMODES ] );

  // sort >
  std::sort( vProfileOpcode.begin(), vProfileOpcode.end(), ProfileOpcode_t() );
  std::sort( vProfileOpmode.begin(), vProfileOpmode.end(), ProfileOpmode_t() );

  Profile_t nOpcodeTotal = 0;
  Profile_t nOpmodeTotal = 0;

  for (iOpcode = 0; iOpcode < NUM_OPCODES; ++iOpcode )
  {
    nOpcodeTotal += vProfileOpcode[ iOpcode ].m_nCount;
  }

  for (iOpmode = 0; iOpmode < NUM_OPMODES; ++iOpmode )
  {
    nOpmodeTotal += vProfileOpmode[ iOpmode ].m_nCount;
  }

  if (nOpcodeTotal < 1.)
  {
    nOpcodeTotal = 1;
    bOpcodeGood = false;
  }

  const char *pColorOperator = "";
  const char *pColorNumber   = "";
  const char *pColorOpcode   = "";
  const char *pColorMnemonic = "";
  const char *pColorOpmode   = "";
  const char *pColorTotal    = "";
  if (! bExport)
  {
    pColorOperator = CHC_ARG_SEP; // grey
    pColorNumber   = CHC_NUM_DEC; // cyan
    pColorOpcode   = CHC_NUM_HEX; // yellow
    pColorMnemonic = CHC_COMMAND; // green
    pColorOpmode   = CHC_USAGE  ; // yellow
    pColorTotal    = CHC_DEFAULT; // white
  }

// Opcode
  if (bExport) // Export = SeperateColumns
    sprintf( pText
      , "\"Percent\"" DELIM "\"Count\"" DELIM "\"Opcode\"" DELIM "\"Mnemonic\"" DELIM "\"Addressing Mode\"\n"
      , sSeperator7, sSeperator2, sSeperator1, sSeperator1 );
  else
    sprintf( pText
      , "Percent" DELIM "Count" DELIM "Mnemonic" DELIM "Addressing Mode\n"
      , sSeperator7, sSeperator2, sSeperator1 );

  pText = ProfileLinePush();

  for (iOpcode = 0; iOpcode < NUM_OPCODES; ++iOpcode )
  {
    ProfileOpcode_t tProfileOpcode = vProfileOpcode.at( iOpcode );

    Profile_t nCount  = tProfileOpcode.m_nCount;

    // Don't spam with empty data if dumping to the console
    if ((! nCount) && (! bExport))
      continue;

    int       nOpcode_val = tProfileOpcode.m_iOpcode;
    int       nOpmode_idx = g_aOpcodes[ nOpcode_val ].nAddressMode;
    double    nPercent = (100. * nCount) / nOpcodeTotal;

    if (bExport)
    {
      sprintf( sOpcode, "\"%02X\"", nOpcode_val );
      sprintf( sAddress, "\"%s\"", g_aOpmodes[ nOpmode_idx ].m_sName );
    }
    else // not qouted if dumping to console
    {
      sprintf( sOpcode, "%02X", nOpcode_val );
      strcpy( sAddress, g_aOpmodes[ nOpmode_idx ].m_sName );
    }

    // BUG: Yeah 100% is off by 1 char. Profiling only one opcode isn't worth fixing this visual alignment bug.
    sprintf( pText,
      "%s%7.4f%s%%" DELIM "%s%9u" DELIM "%s%s" DELIM "%s%s" DELIM "%s%s\n"
      , pColorNumber
      , nPercent
      , pColorOperator
      , sSeperator2
      , pColorNumber
      , static_cast<unsigned int>(nCount), sSeperator2
      , pColorOpcode
      , sOpcode, sSeperator2
      , pColorMnemonic
      , g_aOpcodes[ nOpcode_val ].sMnemonic, sSeperator2
      , pColorOpmode
      , sAddress
    );
    pText = ProfileLinePush();
  }

  if (! bOpcodeGood)
    nOpcodeTotal = 0;

  sprintf( pText
    , "Total:  " DELIM "%s%9u\n"
    , sSeperator2
    , pColorTotal
    , static_cast<unsigned int>(nOpcodeTotal) );
  pText = ProfileLinePush();

  sprintf( pText, "\n" );
  pText = ProfileLinePush();

// Opmode
  if (bExport)
    sprintf( pText
      , "\"Percent\"" DELIM "\"Count\"" DELIM DELIM DELIM "\"Addressing Mode\"\n"
      , sSeperator7, sSeperator2, sSeperator2, sSeperator2 );
  else
  {
    sprintf( pText
      , "Percent" DELIM "Count" DELIM "Addressing Mode\n"
      , sSeperator7, sSeperator2 );
  }
  pText = ProfileLinePush();

  if (nOpmodeTotal < 1)
  {
    nOpmodeTotal = 1.;
    bOpmodeGood = false;
  }

  for (iOpmode = 0; iOpmode < NUM_OPMODES; ++iOpmode )
  {
    ProfileOpmode_t tProfileOpmode = vProfileOpmode.at( iOpmode );
    Profile_t nCount  = tProfileOpmode.m_nCount;

    // Don't spam with empty data if dumping to the console
    if ((! nCount) && (! bExport))
      continue;

    int       nOpmode_idx  = tProfileOpmode.m_iOpmode;
    double    nPercent = (100. * nCount) / nOpmodeTotal;

    if (bExport)
    {
      sprintf( sAddress, "%.*s%.*s\"%.*s\"", int(strlen(sSeperator1)), sSeperator1, int(strlen(sSeperator1)), sSeperator1, int(strlen(g_aOpmodes[ nOpmode_idx ].m_sName)), g_aOpmodes[ nOpmode_idx ].m_sName );
    }
    else // not qouted if dumping to console
    {
      strcpy( sAddress, g_aOpmodes[ nOpmode_idx ].m_sName );
    }

    sprintf( pText
      , "%s%7.4f%s%%" DELIM "%s%9u" DELIM "%s%s\n"
      , pColorNumber
      , nPercent
      , pColorOperator
      , sSeperator2
      , pColorNumber
      , static_cast<unsigned int>(nCount), sSeperator2
      , pColorOpmode
      , sAddress
    );
    pText = ProfileLinePush();
  }

  if (! bOpmodeGood)
    nOpmodeTotal = 0;

  sprintf( pText
    , "Total:  " DELIM "%s%9u\n"
    , sSeperator2
    , pColorTotal
    , static_cast<unsigned int>(nOpmodeTotal) );
  pText = ProfileLinePush();

  sprintf( pText, "===================\n" );
  pText = ProfileLinePush();

  unsigned int cycles = static_cast<unsigned int>(g_nCumulativeCycles - g_nProfileBeginCycles);
  sprintf( pText
    , "Cycles: " DELIM "%s%9u\n"
    , sSeperator2
    , pColorNumber
    , cycles );
  pText = ProfileLinePush();
}
#undef DELIM

void ProfileReset()
{
  int iOpcode;
  int iOpmode;

  for (iOpcode = 0; iOpcode < NUM_OPCODES; iOpcode++ )
  {
    g_aProfileOpcodes[ iOpcode ].m_iOpcode = iOpcode;
    g_aProfileOpcodes[ iOpcode ].m_nCount = 0;
  }

  for (iOpmode = 0; iOpmode < NUM_OPMODES; iOpmode++ )
  {
    g_aProfileOpmodes[ iOpmode ].m_iOpmode = iOpmode;
    g_aProfileOpmodes[ iOpmode ].m_nCount = 0;
  }

  g_nProfileBeginCycles = g_nCumulativeCycles;
}

bool ProfileSave()
{
  bool bStatus = false;

  const std::string sFilename = g_state.sProgramDir + g_FileNameProfile; // TODO: Allow user to decide?

  FILE *hFile = fopen( sFilename.c_str(), "wt" );

  if (hFile)
  {
    char *pText;
    int   nLine = g_nProfileLine;
    int   iLine;

    for( iLine = 0; iLine < nLine; iLine++ )
    {
      pText = ProfileLinePeek( iLine );
      if ( pText )
      {
        fputs( pText, hFile );
      }
    }

    fclose( hFile );
    bStatus = true;
  }

  return bStatus;
}
