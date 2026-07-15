#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include <algorithm>
#include <vector>
#include <string>
#include "Debugger_Cmd_Benchmark.h"
#include "Debugger_Cmd_CPU.h"
#include "Debugger_Assembler.h"
#include "Debug.h"
extern void frame_refresh_status(int);
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Debugger_Console.h"
#include <cstring>
#include <cstdio>

// Globals originally from Debug.cpp
bool g_bBenchmarking = false;
bool g_bProfiling = false;

ProfileOpcode_t g_aProfileOpcodes[ NUM_OPCODES ];
ProfileOpmode_t g_aProfileOpmodes[ NUM_OPMODES ];
uint64_t g_nProfileBeginCycles = 0; // g_nCumulativeCycles // PROFILE RESET

const std::string g_FileNameProfile = "Profile.txt";
int   g_nProfileLine = 0;
char  g_aProfileLine[ NUM_PROFILE_LINES ][ CONSOLE_WIDTH ];

uint32_t extbench = 0;

// Externs
extern uint16_t g_nDisasmCurAddress;
extern uint16_t g_nDisasmTopAddress;
extern uint16_t g_nDisasmBotAddress;
extern int g_nDisasmCurLine;
extern bool g_bDisasmCurBad;

// Implementation ___________________________________________________________

auto CmdBenchmarkStart (int nArgs) -> Update_t
{
  (void)nArgs;
  g_bBenchmarking = true;
  extbench = 0;
  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdBenchmark (int nArgs) -> Update_t
{
  if (! nArgs)
  {
    g_bBenchmarking = false;
  }
  else
  {
    g_bBenchmarking = true;
    extbench = 0;
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdProfileList (int nArgs) -> Update_t;

auto CmdProfile (int nArgs) -> Update_t
{
  if (! nArgs)
  {
    return CmdProfileList( 0 );
  }

  int iArg = 1;
  int iParam = 0;
  bool bFound = FindParam( g_aArgs[ iArg ].sArg, MATCH_EXACT, iParam, _PARAM_PROFILE_BEGIN, _PARAM_PROFILE_END ) > 0;

  if (bFound)
  {
    if (iParam == PARAM_PROFILE_RESET)
    {
      ProfileReset();
    }
    else
    if (iParam == PARAM_PROFILE_SAVE)
    {
      if (ProfileSave())
      {
        char sText[ CONSOLE_WIDTH ];
        ConsoleBufferPushFormat ( sText, " Saved: %s", g_FileNameProfile.c_str() );
      }
    }
    else
    if (iParam == PARAM_PROFILE_LIST)
    {
      return CmdProfileList( 0 );
    }
    else
    {
      g_bProfiling = (iParam == PARAM_PROFILE_ON);
      g_nProfileBeginCycles = g_nCumulativeCycles;
    }
  }
  else
  {
    return Help_Arg_1( CMD_PROFILE );
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto ProfileLinePeek ( int iLine ) -> char *
{
  char *pText = nullptr;

  if (iLine < 0) {
    iLine = 0;
}

  if (iLine <= g_nProfileLine) {
    pText = & g_aProfileLine[ iLine ][ 0 ];
}

  return pText;
}

void ProfileReset()
{
  int iOpcode = 0;
  for( iOpcode = 0; iOpcode < NUM_OPCODES; iOpcode++ )
  {
    g_aProfileOpcodes[ iOpcode ].m_iOpcode = iOpcode;
    g_aProfileOpcodes[ iOpcode ].m_nCount = 0;
  }

  int iOpmode = 0;
  for( iOpmode = 0; iOpmode < NUM_OPMODES; iOpmode++ )
  {
    g_aProfileOpmodes[ iOpmode ].m_iOpmode = iOpmode;
    g_aProfileOpmodes[ iOpmode ].m_nCount = 0;
  }

  g_nProfileLine = 0;
  g_nProfileBeginCycles = g_nCumulativeCycles;
}

void ProfileFormat( bool bSeperateColumns, int eFormatMode )
{
  (void)bSeperateColumns;
  (void)eFormatMode;
  int iOpcode = 0;
  int iOpmode = 0;

  bool bOpcodeGood = true;
  bool bOpmodeGood = true;

  std::vector< ProfileOpcode_t > vProfileOpcode( &g_aProfileOpcodes[0], &g_aProfileOpcodes[ NUM_OPCODES ] );
  std::vector< ProfileOpmode_t > vProfileOpmode( &g_aProfileOpmodes[0], &g_aProfileOpmodes[ NUM_OPMODES ] );

  // sort >
  std::sort( vProfileOpcode.begin(), vProfileOpcode.end(), ProfileOpcode_t() );
  std::sort( vProfileOpmode.begin(), vProfileOpmode.end(), ProfileOpmode_t() );

  g_nProfileLine = 0;
  char *pText = & g_aProfileLine[ 0 ][ 0 ];

  uint64_t nTotalCycles = g_nCumulativeCycles - g_nProfileBeginCycles;
  sprintf( pText, "Cycles: %llu\n", static_cast<unsigned long long>(nTotalCycles) );
  g_nProfileLine++;

  while (bOpcodeGood || bOpmodeGood)
  {
    pText = & g_aProfileLine[ g_nProfileLine ][ 0 ];
    pText[ 0 ] = 0;

    if (iOpcode < NUM_OPCODES)
    {
      if (vProfileOpcode.at( static_cast<size_t>(iOpcode) ).m_nCount > 0)
      {
        sprintf( pText, "%s: %llu",
          g_aOpcodes65C02[ vProfileOpcode.at( static_cast<size_t>(iOpcode) ).m_iOpcode ].sMnemonic,
          static_cast<unsigned long long>(vProfileOpcode.at( static_cast<size_t>(iOpcode) ).m_nCount)
        );
      }
      else
      {
        bOpcodeGood = false;
      }
    }

    if (iOpmode < NUM_OPMODES)
    {
      if (vProfileOpmode.at( static_cast<size_t>(iOpmode) ).m_nCount > 0)
      {
        char sOpmode[ CONSOLE_WIDTH ];
        sprintf( sOpmode, "  %s: %llu",
          g_aOpmodes[ static_cast<size_t>(vProfileOpmode.at( static_cast<size_t>(iOpmode) ).m_iOpmode) ].m_sName,
          static_cast<unsigned long long>(vProfileOpmode.at( static_cast<size_t>(iOpmode) ).m_nCount)
        );
        strcat( pText, sOpmode );
      }
      else
      {
        bOpmodeGood = false;
      }
    }

    if (pText[ 0 ])
    {
      strcat( pText, "\n" );
      g_nProfileLine++;
    }

    iOpcode++;
    iOpmode++;

    if (g_nProfileLine >= (NUM_PROFILE_LINES - 1)) {
      break;
}
  }
}

auto CmdProfileList (int nArgs) -> Update_t
{
  (void)nArgs;
  ProfileFormat( true, 0 );

  int nLines = MIN( g_nProfileLine, g_nConsoleDisplayLines - 1 );
  return ConsoleBufferTryUnpause( nLines );
}

auto ProfileSave () -> bool
{
  bool bStatus = false;
  FilePtr hFile(fopen( g_FileNameProfile.c_str(), "w" ), fclose);

  if ( hFile )
  {
    ProfileFormat( true, 0 );

    char *pText = nullptr;
    int   nLine = g_nProfileLine;
    int   iLine = 0;

    for( iLine = 0; iLine < nLine; iLine++ )
    {
      pText = ProfileLinePeek( iLine );
      if ( pText )
      {
        fputs( pText, hFile.get() );
      }
    }

    bStatus = true;
  }

  return bStatus;
}
