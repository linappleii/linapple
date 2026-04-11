#include "Common.h"
#include "Debugger_Cmd_CPU.h"
#include "apple2/CPU.h"
#include "Debug.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Debugger_Symbols.h"
#include "Debugger_Cmd_Config.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Cmd_Benchmark.h"
#include "Debugger_Cmd_Output.h"
#include "Debugger_Assembler.h"
#include "Video.h"
#include "Frame.h"
#include "apple2/SoundCore.h"
#include "apple2/Keyboard.h"
#include "Log.h"
#include "apple2/Memory.h"
#include "SDL3/SDL.h"
#include <cstddef>
#include <cstdio>
#include <cassert>

// Definitions
int g_nDebugSteps = 0;
unsigned int g_nDebugStepCycles = 0;
int g_nDebugStepStart = 0;
int g_nDebugStepUntil = -1;
int g_nDebugSkipStart = 0;
int g_nDebugSkipLen = 0;

bool g_bDebugFullSpeed = false;
bool g_bLastGoCmdWasFullSpeed = false;
bool g_bGoCmd_ReinitFlag = false;

FILE *g_hTraceFile = NULL;
bool g_bTraceHeader = false;
bool g_bTraceFileWithVideoScanner = false;
char g_sFileNameTrace[] = "Trace.txt";

extern unsigned short g_nDisasmCurAddress;
extern int g_nDisasmCurLine;

extern ProfileOpcode_t g_aProfileOpcodes[ NUM_OPCODES ];
extern ProfileOpmode_t g_aProfileOpmodes[ NUM_OPMODES ];

extern int g_iDebugBreakOnOpcode;
extern int g_bDebugBreakpointHit;

extern unsigned short g_nDisasmTopAddress;
extern unsigned short g_nDisasmBotAddress;

extern uint32_t g_nVideoClockHorz;
extern uint32_t g_nVideoClockVert;

extern VideoScannerDisplayInfo g_videoScannerDisplayInfo;

void DisasmCalcTopBotAddress();
bool IsDebugBreakOnInvalid(int iOpcodeType);
uint16_t NTSC_VideoGetScannerAddressForDebugger();
void VideoRefreshScreen(int bVideoModeFlags, bool bForceRedraw);
bool VideoGetSWPAGE2();
bool VideoGetSWMIXED();

// Implementation
// CPU ____________________________________________________________________________________________
// CPU Step, Trace ________________________________________________________________________________

//===========================================================================
Update_t CmdGo (int nArgs, const bool bFullSpeed)
{
  // G StopAddress [SkipAddress,Length]
  // Example:
  //  G C600 FA00,FFFF
  // TODO: G addr1,len   addr3,len
  // TODO: G addr1:addr2 addr3:addr4

  const int kCmdGo = !bFullSpeed ? CMD_GO_NORMAL_SPEED : CMD_GO_FULL_SPEED;

  g_nDebugSteps = -1;
  g_nDebugStepCycles  = 0;
  g_nDebugStepStart = regs.pc;
  g_nDebugStepUntil = nArgs ? g_aArgs[1].nValue : -1;
  g_nDebugSkipStart = -1;
  g_nDebugSkipLen   = -1;

  if (nArgs > 4)
    return Help_Arg_1( kCmdGo );

  //     G StopAddress [SkipAddress,Len]
  // Old   1            2           2
  //     G addr addr [, len]
  // New   1    2     3 4
  if (nArgs > 1)
  {
    int iArg = 2;
    g_nDebugSkipStart = g_aArgs[ iArg ].nValue;

#if DEBUG_VAL_2
    unsigned short nAddress     = g_aArgs[ iArg ].nVal2;
#endif
    int nLen = 0;
    int nEnd = 0;

    if (nArgs > 2)
    {
      if (g_aArgs[ iArg + 1 ].eToken == TOKEN_COMMA)
      {
        if (nArgs > 3)
        {
          nLen = g_aArgs[ iArg + 2 ].nValue;
          nEnd = g_nDebugSkipStart + nLen;
          if (nEnd > (int) _6502_MEM_END)
            nEnd = _6502_MEM_END + 1;
        }
        else
        {
          return Help_Arg_1( kCmdGo );
        }
      }
      else
      if (g_aArgs[ iArg+ 1 ].eToken == TOKEN_COLON)
      {
        nEnd = g_aArgs[ iArg + 2 ].nValue + 1;
      }
      else
        return Help_Arg_1( kCmdGo );
    }
    else
      return Help_Arg_1( kCmdGo );

    nLen = nEnd - g_nDebugSkipStart;
    if (nLen < 0)
      nLen = -nLen;
    g_nDebugSkipLen = nLen;
    g_nDebugSkipLen &= _6502_MEM_END;

#if _DEBUG
  char sText[ CONSOLE_WIDTH ];
  ConsoleBufferPushFormat( sText, "Start: %04X,%04X  End: %04X  Len: %04X",
    g_nDebugSkipStart, g_nDebugSkipLen, nEnd, nLen );
  ConsoleBufferToDisplay();
#endif
  }

//  unsigned short nAddressSymbol = 0;
//  bool bFoundSymbol = FindAddressFromSymbol( g_aArgs[1].sArg, & nAddressSymbol );
//  if (bFoundSymbol)
//    g_nDebugStepUntil = nAddressSymbol;

//  if (!g_nDebugStepUntil)
//    g_nDebugStepUntil = GetAddress(g_aArgs[1].sArg);

  g_bDebuggerEatKey = true;

  g_bDebugFullSpeed = bFullSpeed;
  g_bLastGoCmdWasFullSpeed = bFullSpeed;
  g_bGoCmd_ReinitFlag = true;

  g_state.mode = MODE_STEPPING;
  FrameRefreshStatus(DRAW_TITLE);

  SoundCore_SetFade(FADE_IN);

  return UPDATE_CONSOLE_DISPLAY;
}

Update_t CmdGoNormalSpeed (int nArgs)
{
  return CmdGo(nArgs, false);
}

Update_t CmdGoFullSpeed (int nArgs)
{
  return CmdGo(nArgs, true);
}

//===========================================================================
Update_t CmdStackPop (int nArgs)
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdStackPopPseudo (int nArgs)
{
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdStepOver (int nArgs)
{
  // assert( g_nDisasmCurAddress == regs.pc );

//  g_nDebugSteps = nArgs ? g_aArgs[1].nValue : 1;
  unsigned short nDebugSteps = nArgs ? g_aArgs[1].nValue : 1;

  while (nDebugSteps -- > 0)
  {
    int nOpcode = *(mem + regs.pc); // g_nDisasmCurAddress
  //  int eMode = g_aOpcodes[ nOpcode ].addrmode;
  //  int nByte = g_aOpmodes[eMode]._nBytes;
  //  if ((eMode ==  AM_A) &&

    CmdTrace(0);
    if (nOpcode == OPCODE_JSR)
    {
      CmdStepOut(0);
      g_nDebugSteps = 0xFFFF;
      while (g_nDebugSteps != 0)
        DebugContinueStepping(true);
    }
  }

  return UPDATE_ALL;
}

//===========================================================================
Update_t CmdStepOut (int nArgs)
{
  (void)nArgs;
  // TODO: "RET" should probably pop the Call stack
  // Also see: CmdCursorJumpRetAddr
  unsigned short nAddress;
  if (_6502_GetStackReturnAddress( nAddress ))
  {
    nArgs = _Arg_1( nAddress );
    g_aArgs[1].sArg[0] = 0;
    CmdGo( 1, true );
  }

  return UPDATE_ALL;
}

//===========================================================================
Update_t CmdTrace (int nArgs)
{
  g_nDebugSteps = nArgs ? g_aArgs[1].nValue : 1;
  g_nDebugStepCycles  = 0;
  g_nDebugStepStart = regs.pc;
  g_nDebugStepUntil = -1;
  g_state.mode = MODE_STEPPING;
  FrameRefreshStatus(DRAW_TITLE);
  DebugContinueStepping(true);

  return UPDATE_ALL; // TODO: Verify // 0
}

//===========================================================================
Update_t CmdTraceFile (int nArgs)
{
  char sText[ CONSOLE_WIDTH ] = "";

  if (g_hTraceFile)
  {
    fclose( g_hTraceFile );
    g_hTraceFile = NULL;

    ConsoleBufferPush( "Trace stopped." );
  }
  else
  {
    std::string sFileName;

    if (nArgs)
      sFileName = g_aArgs[1].sArg;
    else
      sFileName = g_sFileNameTrace;

    g_bTraceFileWithVideoScanner = (nArgs >= 2);

    const std::string sFilePath = g_state.sCurrentDir + sFileName;

    g_hTraceFile = fopen( sFilePath.c_str(), "wt" );

    if (g_hTraceFile)
    {
      const char* pTextHdr = g_bTraceFileWithVideoScanner ? "Trace (with video info) started: %s"
            : "Trace started: %s";
      ConsoleBufferPushFormat( sText, pTextHdr, sFilePath.c_str() );
      g_bTraceHeader = true;
    }
    else
    {
      ConsoleBufferPushFormat( sText, "Trace ERROR: %s", sFilePath.c_str() );
    }
  }

  ConsoleBufferToDisplay();

  return UPDATE_ALL; // TODO: Verify // 0
}

//===========================================================================
Update_t CmdTraceLine (int nArgs)
{
  g_nDebugSteps = nArgs ? g_aArgs[1].nValue : 1;
  g_nDebugStepCycles  = 1;
  g_nDebugStepStart = regs.pc;
  g_nDebugStepUntil = -1;

  g_state.mode = MODE_STEPPING;
  FrameRefreshStatus(DRAW_TITLE);
  DebugContinueStepping(true);

  return UPDATE_ALL; // TODO: Verify // 0
}




// Unassemble
//===========================================================================
Update_t CmdUnassemble (int nArgs)
{
  if (! nArgs)
    return Help_Arg_1( CMD_UNASSEMBLE );

  unsigned short nAddress = g_aArgs[1].nValue;
  g_nDisasmTopAddress = nAddress;

  DisasmCalcCurFromTopAddress();
  DisasmCalcBotFromTopAddress();

  return UPDATE_DISASM;
}


//===========================================================================
Update_t CmdKey (int nArgs)
{
  uint8_t code = nArgs ? (g_aArgs[1].nValue ? (uint8_t)g_aArgs[1].nValue : (uint8_t)g_aArgs[1].sArg[0]) : (uint8_t)' ';
  KeybQueueKeypress(code);
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
Update_t CmdIn (int nArgs)
{
  if (!nArgs)
    return Help_Arg_1( CMD_IN );

  unsigned short nAddress = g_aArgs[1].nValue;

  IOMap_Dispatch(regs.pc, nAddress & 0xFFFF, 0, 0, 0);

  return UPDATE_CONSOLE_DISPLAY; // TODO: Verify // 1
}


//===========================================================================
Update_t CmdJSR (int nArgs)
{
  if (! nArgs)
    return Help_Arg_1( CMD_JSR );

  unsigned short nAddress = g_aArgs[1].nValue & _6502_MEM_END;

  // Mark Stack Page as dirty
  *(memdirty+(regs.sp >> 8)) = 1;

  // Push PC onto stack
  *(mem + regs.sp) = ((regs.pc >> 8) & 0xFF);
  regs.sp--;

  *(mem + regs.sp) = ((regs.pc >> 0) - 1) & 0xFF;
  regs.sp--;


  // Jump to new address
  regs.pc = nAddress;

  return UPDATE_ALL;
}


//===========================================================================
Update_t CmdNOP (int nArgs)
{
  (void)nArgs;
  int iOpcode;
  int iOpmode;
  int nOpbytes;

  _6502_GetOpcodeOpmodeOpbyte( iOpcode, iOpmode, nOpbytes );

  while (nOpbytes--)
  {
    *(mem+regs.pc + nOpbytes) = 0xEA;
  }

  return UPDATE_ALL;
}

//===========================================================================
Update_t CmdOut (int nArgs)
{
//  if ((!nArgs) ||
//      ((g_aArgs[1].sArg[0] != '0') && (!g_aArgs[1].nValue) && (!GetAddress(g_aArgs[1].sArg))))
//     return DisplayHelp(CmdInput);

  if (!nArgs)
    Help_Arg_1( CMD_OUT );

  unsigned short nAddress = g_aArgs[1].nValue;

  IOWrite[ (nAddress>>4) & 0xF ] (regs.pc, nAddress & 0xFF, 1, g_aArgs[2].nValue & 0xFF, 0);

  return UPDATE_ALL;
}

Update_t CmdRegisterSet (int nArgs)
{
  if (nArgs < 2) // || ((g_aArgs[2].sArg[0] != '0') && !g_aArgs[2].nValue))
  {
    return Help_Arg_1( CMD_REGISTER_SET );
  }
  else
  {
    char *pName = g_aArgs[1].sArg;
    int iParam;
    if (FindParam( pName, MATCH_EXACT, iParam, _PARAM_REGS_BEGIN, _PARAM_REGS_END ))
    {
      int iArg = 2;
      if (g_aArgs[ iArg ].eToken == TOKEN_EQUAL)
        iArg++;

      if (iArg > nArgs)
        return Help_Arg_1( CMD_REGISTER_SET );

      unsigned char b = (unsigned char)(g_aArgs[ iArg ].nValue & 0xFF);
      unsigned short w = (unsigned short)(g_aArgs[ iArg ].nValue & 0xFFFF);

      switch (iParam)
      {
        case PARAM_REG_A : regs.a  = b;    break;
        case PARAM_REG_PC: regs.pc = w; g_nDisasmCurAddress = regs.pc; DisasmCalcTopBotAddress(); break;
        case PARAM_REG_SP: regs.sp = b | 0x100; break;
        case PARAM_REG_X : regs.x  = b; break;
        case PARAM_REG_Y : regs.y  = b; break;
        default:        return Help_Arg_1( CMD_REGISTER_SET );
      }
    }
  }

//  g_nDisasmCurAddress = regs.pc;
//  DisasmCalcTopBotAddress();

  return UPDATE_ALL; // 1
}

//===========================================================================
void OutputTraceLine ()
{
#ifdef TODO // Not supported for Linux yet
  DisasmLine_t line;
  GetDisassemblyLine( regs.pc, line );

  char sDisassembly[ CONSOLE_WIDTH ]; // DrawDisassemblyLine( 0,regs.pc, sDisassembly); // Get Disasm String
  FormatDisassemblyLine( line, sDisassembly, CONSOLE_WIDTH );

  char sFlags[ _6502_NUM_FLAGS + 1 ]; DrawFlags( 0, regs.ps, sFlags ); // Get Flags String

  if (!g_hTraceFile)
    return;

  if (g_bTraceHeader)
  {
    g_bTraceHeader = false;

    if (g_bTraceFileWithVideoScanner)
    {
      fprintf( g_hTraceFile,
//        "0000 0000 0000 00   00 00 00 0000 --------  0000:90 90 90  NOP"
        "Vert Horz Addr Data A: X: Y: SP:  Flags     Addr:Opcode    Mnemonic\n");
    }
    else
    {
      fprintf( g_hTraceFile,
//        "00 00 00 0000 --------  0000:90 90 90  NOP"
        "A: X: Y: SP:  Flags     Addr:Opcode    Mnemonic\n");
    }
  }

  char sTarget[ 16 ];
  if (line.bTargetValue)
  {
    sprintf( sTarget, "%s:%s"
      , line.sTargetPointer
      , line.sTargetValue
    );
  }

  if (g_bTraceFileWithVideoScanner)
  {
    uint16_t addr = NTSC_VideoGetScannerAddressForDebugger();
    unsigned char data = mem[addr];

    fprintf( g_hTraceFile,
      "%04X %04X %04X   %02X %02X %02X %02X %04X %s  %s\n",
      g_nVideoClockVert,
      g_nVideoClockHorz,
      addr,
      data,
      (unsigned)regs.a,
      (unsigned)regs.x,
      (unsigned)regs.y,
      (unsigned)regs.sp,
      (char*) sFlags
      , sDisassembly
      //, sTarget // TODO: Show target?
    );
  }
  else
  {
    fprintf( g_hTraceFile,
      "%02X %02X %02X %04X %s  %s\n",
      (unsigned)regs.a,
      (unsigned)regs.x,
      (unsigned)regs.y,
      (unsigned)regs.sp,
      (char*) sFlags
      , sDisassembly
      //, sTarget // TODO: Show target?
    );
  }
#endif
}

static void CheckBreakOpcode( int iOpcode )
{
  if (iOpcode == 0x00)  // BRK
    IsDebugBreakOnInvalid( AM_IMPLIED );

  if (g_aOpcodes[iOpcode].sMnemonic[0] >= 'a')  // All 6502/65C02 undocumented opcodes mnemonics are lowercase strings!
  {
    // TODO: Translate g_aOpcodes[iOpcode].nAddressMode into {AM_1, AM_2, AM_3}
    IsDebugBreakOnInvalid( AM_1 );
  }

  // User wants to enter debugger on specific opcode? (NB. Can't be BRK)
  if (g_iDebugBreakOnOpcode && g_iDebugBreakOnOpcode == iOpcode)
    g_bDebugBreakpointHit |= BP_HIT_OPCODE;
}

void DebugContinueStepping(const bool bCallerWillUpdateDisplay)
{
  static bool bForceSingleStepNext = false; // Allow at least one instruction to execute so we don't trigger on the same invalid opcode

  if (g_nDebugSkipLen > 0)
  {
    if ((regs.pc >= g_nDebugSkipStart) && (regs.pc < (g_nDebugSkipStart + g_nDebugSkipLen)))
    {
      // Enter turbo debugger mode -- UI not updated, etc.
      g_nDebugSteps = -1;
      g_state.mode = MODE_STEPPING;
    }
    else
    {
      // Enter normal debugger mode -- UI updated every instruction, etc.
      g_nDebugSteps = 1;
      g_state.mode = MODE_STEPPING;
    }
  }

  bool bDoSingleStep = true;

  if ( (g_nDebugSteps != 0) || (bForceSingleStepNext) )
  {
    if (! bForceSingleStepNext)
    {
      if (g_hTraceFile)
        OutputTraceLine();

      g_bDebugBreakpointHit = BP_HIT_NONE;

      if ( MemIsAddrCodeMemory(regs.pc) )
      {
        unsigned char nOpcode = *(mem+regs.pc);

        // Update profiling stats
        int  nOpmode = g_aOpcodes[ nOpcode ].nAddressMode;
        g_aProfileOpcodes[ nOpcode ].m_nCount++;
        g_aProfileOpmodes[ nOpmode ].m_nCount++;

        CheckBreakOpcode( nOpcode );  // Can set g_bDebugBreakpointHit
      }
      else
      {
        g_bDebugBreakpointHit = BP_HIT_PC_READ_FLOATING_BUS_OR_IO_MEM;
      }

      if (g_bDebugBreakpointHit)
      {
        bDoSingleStep = false;
        bForceSingleStepNext = true;  // Allow next single-step (after this) to execute
      }
    }

    if (bDoSingleStep)
    {
      if (g_nDebugSteps > 0)
        g_nDebugSteps--;

      bForceSingleStepNext = false;

      // Single-step the CPU
      if (g_state.mode == MODE_DEBUG)
        g_state.mode = MODE_STEPPING;

      // CPU_Step();
    }
  }

  if ( (g_nDebugSteps == 0) && (! bForceSingleStepNext) )
  {
    g_state.mode = MODE_DEBUG;
    g_nDebugSteps = 0;

    DisasmCalcTopBotAddress();

    if (!bCallerWillUpdateDisplay)
      UpdateDisplay( UPDATE_ALL );
  }
}

void DebugStopStepping(void)
{
  assert(g_state.mode == MODE_STEPPING);

  if (g_state.mode != MODE_STEPPING)
    return;

  g_nDebugSteps = 0; // On next DebugContinueStepping(), stop single-stepping and transition to MODE_DEBUG
  ClearTempBreakpoints();
}


// Output _________________________________________________________________________________________

