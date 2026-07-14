#include "Debugger_Cmd_CPU.h"

#include "Debug.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
extern void FrameRefreshStatus(int);
#include <cassert>
#include <cstddef>
#include <cstdio>

#include "Debugger_Assembler.h"
#include "Debugger_Cmd_Benchmark.h"
#include "Debugger_Cmd_Config.h"
#include "Debugger_Cmd_Output.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Display.h"
#include "Debugger_Help.h"
#include "Debugger_Parser.h"
#include "Debugger_Symbols.h"
#include "Video.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/AudioMixer.h"
#include "core/Log.h"
#include "core/Peripheral.h"

// Definitions
int g_nDebugSteps = 0;
uint32_t g_nDebugStepCycles = 0;
int g_nDebugStepStart = 0;
int g_nDebugStepUntil = -1;
int g_nDebugSkipStart = 0;
int g_nDebugSkipLen = 0;

bool g_bDebugFullSpeed = false;
bool g_bLastGoCmdWasFullSpeed = false;
bool g_bGoCmd_ReinitFlag = false;

FILE* g_hTraceFile = nullptr;
bool g_bTraceHeader = false;
bool g_bTraceFileWithVideoScanner = false;
char g_sFileNameTrace[] = "Trace.txt";

extern uint16_t g_nDisasmCurAddress;
extern int g_nDisasmCurLine;

extern ProfileOpcode_t g_aProfileOpcodes[NUM_OPCODES];
extern ProfileOpmode_t g_aProfileOpmodes[NUM_OPMODES];

extern int g_iDebugBreakOnOpcode;
extern int g_bDebugBreakpointHit;
extern int g_nDebugBreakOnInvalid;

extern uint16_t g_nDisasmTopAddress;
extern uint16_t g_nDisasmBotAddress;

extern uint32_t g_nVideoClockHorz;
extern uint32_t g_nVideoClockVert;

extern VideoScannerDisplayInfo g_videoScannerDisplayInfo;

void DisasmCalcTopBotAddress();
auto IsDebugBreakOnInvalid(int iOpcodeType) -> bool;
auto NTSC_VideoGetScannerAddressForDebugger() -> uint16_t;
void VideoRefreshScreen(int bVideoModeFlags, bool bForceRedraw);
auto VideoGetSWPAGE2() -> bool;
auto VideoGetSWMIXED() -> bool;

// Implementation
// CPU
// ____________________________________________________________________________________________
// CPU Step, Trace
// ________________________________________________________________________________

//===========================================================================
auto CmdGo(int nArgs, const bool bFullSpeed) -> Update_t {
  // G StopAddress [SkipAddress,Length]
  // Example:
  //  G C600 FA00,FFFF
  // TODO: G addr1,len   addr3,len
  // TODO: G addr1:addr2 addr3:addr4

  const int kCmdGo = !bFullSpeed ? CMD_GO_NORMAL_SPEED : CMD_GO_FULL_SPEED;

  g_nDebugSteps = -1;
  g_nDebugStepCycles = 0;
  g_nDebugStepStart = CpuGetRegisters()->pc;
  g_nDebugStepUntil = nArgs ? g_aArgs[1].nValue : -1;
  g_nDebugSkipStart = -1;
  g_nDebugSkipLen = -1;

  if (nArgs > 4) {
    return Help_Arg_1(kCmdGo);
  }

  //     G StopAddress [SkipAddress,Len]
  // Old   1            2           2
  //     G addr addr [, len]
  // New   1    2     3 4
  if (nArgs > 1) {
    int iArg = 2;
    g_nDebugSkipStart = g_aArgs[iArg].nValue;

#if DEBUG_VAL_2
    uint16_t nAddress = g_aArgs[iArg].nVal2;
#endif
    int nLen = 0;
    int nEnd = 0;

    if (nArgs > 2) {
      if (g_aArgs[iArg + 1].eToken == TOKEN_COMMA) {
        if (nArgs > 3) {
          nLen = g_aArgs[iArg + 2].nValue;
          nEnd = g_nDebugSkipStart + nLen;
          if (nEnd > static_cast<int>(_6502_MEM_END)) {
            nEnd = _6502_MEM_END + 1;
          }
        } else {
          return Help_Arg_1(kCmdGo);
        }
      } else if (g_aArgs[iArg + 1].eToken == TOKEN_COLON) {
        nEnd = g_aArgs[iArg + 2].nValue + 1;
      } else {
        return Help_Arg_1(kCmdGo);
      }
    } else {
      return Help_Arg_1(kCmdGo);
    }

    nLen = nEnd - g_nDebugSkipStart;
    if (nLen < 0) {
      nLen = -nLen;
    }
    g_nDebugSkipLen = nLen;
    g_nDebugSkipLen &= _6502_MEM_END;

#if _DEBUG
    char sText[CONSOLE_WIDTH];
    ConsoleBufferPushFormat(sText, "Start: %04X,%04X  End: %04X  Len: %04X",
                            g_nDebugSkipStart, g_nDebugSkipLen, nEnd, nLen);
    ConsoleBufferToDisplay();
#endif
  }

  //  uint16_t nAddressSymbol = 0;
  //  bool bFoundSymbol = FindAddressFromSymbol( g_aArgs[1].sArg, &
  //  nAddressSymbol ); if (bFoundSymbol)
  //    g_nDebugStepUntil = nAddressSymbol;

  //  if (!g_nDebugStepUntil)
  //    g_nDebugStepUntil = GetAddress(g_aArgs[1].sArg);

  g_bDebuggerEatKey = true;

  g_bDebugFullSpeed = bFullSpeed;
  g_bLastGoCmdWasFullSpeed = bFullSpeed;
  g_bGoCmd_ReinitFlag = true;

  g_state.mode = MODE_STEPPING;
  FrameRefreshStatus(DRAW_TITLE);

  audio_mixer_set_fade(fade_in);

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdGoNormalSpeed(int nArgs) -> Update_t { return CmdGo(nArgs, false); }

auto CmdGoFullSpeed(int nArgs) -> Update_t { return CmdGo(nArgs, true); }

auto CmdBreakInvalid(int nArgs) -> Update_t {
  if (nArgs == 0) {
    g_nDebugBreakOnInvalid ^= 1;
  } else {
    g_nDebugBreakOnInvalid = g_aArgs[1].nValue != 0;
  }
  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdBreakOpcode(int nArgs) -> Update_t {
  if (nArgs == 0) {
    g_iDebugBreakOnOpcode = 0;
  } else {
    g_iDebugBreakOnOpcode = g_aArgs[1].nValue & 0xFF;
  }
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdStackPop(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdStackPopPseudo(int nArgs) -> Update_t {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdStepOver(int nArgs) -> Update_t {
  // assert( g_nDisasmCurAddress == CpuGetRegisters()->pc );

  //  g_nDebugSteps = nArgs ? g_aArgs[1].nValue : 1;
  uint16_t nDebugSteps = nArgs ? g_aArgs[1].nValue : 1;

  while (nDebugSteps-- > 0) {
    int nOpcode = *(mem + CpuGetRegisters()->pc);  // g_nDisasmCurAddress
    //  int eMode = g_aOpcodes[ nOpcode ].addrmode;
    //  int nByte = g_aOpmodes[eMode]._nBytes;
    //  if ((eMode ==  AM_A) &&

    CmdTrace(0);
    if (nOpcode == OPCODE_JSR) {
      CmdStepOut(0);
      g_nDebugSteps = 0xFFFF;
      while (g_nDebugSteps != 0) {
        DebugContinueStepping(true);
      }
    }
  }

  return UPDATE_ALL;
}

//===========================================================================
auto CmdStepOut(int nArgs) -> Update_t {
  (void)nArgs;
  // TODO: "RET" should probably pop the Call stack
  // Also see: CmdCursorJumpRetAddr
  uint16_t nAddress = 0;
  if (_6502_GetStackReturnAddress(nAddress)) {
    nArgs = _Arg_1(nAddress);
    g_aArgs[1].sArg[0] = 0;
    CmdGo(1, true);
  }

  return UPDATE_ALL;
}

//===========================================================================
auto CmdTrace(int nArgs) -> Update_t {
  g_nDebugSteps = nArgs ? g_aArgs[1].nValue : 1;
  g_nDebugStepCycles = 0;
  g_nDebugStepStart = CpuGetRegisters()->pc;
  g_nDebugStepUntil = -1;
  g_state.mode = MODE_STEPPING;
  FrameRefreshStatus(DRAW_TITLE);
  DebugContinueStepping(true);

  return UPDATE_ALL;  // TODO: Verify // 0
}

//===========================================================================
auto CmdTraceFile(int nArgs) -> Update_t {
  char sText[CONSOLE_WIDTH] = "";

  if (g_hTraceFile) {
    fclose(g_hTraceFile);
    g_hTraceFile = nullptr;

    ConsoleBufferPush("Trace stopped.");
  } else {
    std::string sFileName;

    if (nArgs) {
      sFileName = g_aArgs[1].sArg;
    } else {
      sFileName = g_sFileNameTrace;
    }

    g_bTraceFileWithVideoScanner = (nArgs >= 2);

    const std::string sFilePath =
        std::string(g_state.sCurrentDir.data()) + sFileName;

    g_hTraceFile = fopen(sFilePath.c_str(), "wt");

    if (g_hTraceFile) {
      const char* pTextHdr = g_bTraceFileWithVideoScanner
                                 ? "Trace (with video info) started: %s"
                                 : "Trace started: %s";
      ConsoleBufferPushFormat(sText, pTextHdr, sFilePath.c_str());
      g_bTraceHeader = true;
    } else {
      ConsoleBufferPushFormat(sText, "Trace ERROR: %s", sFilePath.c_str());
    }
  }

  ConsoleBufferToDisplay();

  return UPDATE_ALL;  // TODO: Verify // 0
}

//===========================================================================
auto CmdTraceLine(int nArgs) -> Update_t {
  g_nDebugSteps = nArgs ? g_aArgs[1].nValue : 1;
  g_nDebugStepCycles = 1;
  g_nDebugStepStart = CpuGetRegisters()->pc;
  g_nDebugStepUntil = -1;

  g_state.mode = MODE_STEPPING;
  FrameRefreshStatus(DRAW_TITLE);
  DebugContinueStepping(true);

  return UPDATE_ALL;  // TODO: Verify // 0
}

// Unassemble
//===========================================================================
auto CmdUnassemble(int nArgs) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(CMD_UNASSEMBLE);
  }

  uint16_t nAddress = g_aArgs[1].nValue;
  g_nDisasmTopAddress = nAddress;

  DisasmCalcCurFromTopAddress();
  DisasmCalcBotFromTopAddress();

  return UPDATE_DISASM;
}

//===========================================================================
auto CmdKey(int nArgs) -> Update_t {
  uint8_t code =
      nArgs ? (g_aArgs[1].nValue ? static_cast<uint8_t>(g_aArgs[1].nValue)
                                 : static_cast<uint8_t>(g_aArgs[1].sArg[0]))
            : static_cast<uint8_t>(' ');

  // Send key-down event
  KeyboardEvent_t ev = {code, 1U, 0, 0, 0, 0, {0, 0, 0}};
  Peripheral_Command(0, keyboard_cmd_event, &ev, sizeof(ev));

  // Send key-up event immediately to simulate a momentary press
  ev.is_down = 0U;
  Peripheral_Command(0, keyboard_cmd_event, &ev, sizeof(ev));

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdIn(int nArgs) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(CMD_IN);
  }

  uint16_t nAddress = g_aArgs[1].nValue;

  IOMap_Dispatch(CpuGetRegisters()->pc, nAddress & 0xFFFF, 0, 0, 0);

  return UPDATE_CONSOLE_DISPLAY;  // TODO: Verify // 1
}

//===========================================================================
auto CmdJSR(int nArgs) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(CMD_JSR);
  }

  uint16_t nAddress = g_aArgs[1].nValue & _6502_MEM_END;

  // Mark Stack Page as dirty
  *(memdirty + (CpuGetRegisters()->sp >> 8)) = 1;

  // Push PC onto stack
  *(mem + CpuGetRegisters()->sp) = ((CpuGetRegisters()->pc >> 8) & 0xFF);
  CpuGetRegisters()->sp--;

  *(mem + CpuGetRegisters()->sp) = ((CpuGetRegisters()->pc >> 0) - 1) & 0xFF;
  CpuGetRegisters()->sp--;

  // Jump to new address
  CpuGetRegisters()->pc = nAddress;

  return UPDATE_ALL;
}

//===========================================================================
auto CmdNOP(int nArgs) -> Update_t {
  (void)nArgs;
  int iOpcode = 0;
  int iOpmode = 0;
  int nOpbytes = 0;

  _6502_GetOpcodeOpmodeOpbyte(iOpcode, iOpmode, nOpbytes);

  while (nOpbytes--) {
    *(mem + CpuGetRegisters()->pc + nOpbytes) = 0xEA;
  }

  return UPDATE_ALL;
}

//===========================================================================
auto CmdOut(int nArgs) -> Update_t {
  //  if ((!nArgs) ||
  //      ((g_aArgs[1].sArg[0] != '0') && (!g_aArgs[1].nValue) &&
  //      (!GetAddress(g_aArgs[1].sArg))))
  //     return DisplayHelp(CmdInput);

  if (!nArgs) {
    Help_Arg_1(CMD_OUT);
  }

  uint16_t nAddress = g_aArgs[1].nValue;

  IOWrite[(nAddress >> 4) & 0xF](CpuGetRegisters()->pc, nAddress & 0xFF, 1,
                                 g_aArgs[2].nValue & 0xFF, 0);

  return UPDATE_ALL;
}

auto CmdRegisterSet(int nArgs) -> Update_t {
  if (nArgs < 2)  // || ((g_aArgs[2].sArg[0] != '0') && !g_aArgs[2].nValue))
  {
    return Help_Arg_1(CMD_REGISTER_SET);
  } else {
    char* pName = g_aArgs[1].sArg;
    int iParam = 0;
    if (FindParam(pName, MATCH_EXACT, iParam, _PARAM_REGS_BEGIN,
                  _PARAM_REGS_END)) {
      int iArg = 2;
      if (g_aArgs[iArg].eToken == TOKEN_EQUAL) {
        iArg++;
      }

      if (iArg > nArgs) {
        return Help_Arg_1(CMD_REGISTER_SET);
      }

      auto b = static_cast<uint8_t>(g_aArgs[iArg].nValue & 0xFF);
      auto w = static_cast<uint16_t>(g_aArgs[iArg].nValue & 0xFFFF);

      switch (iParam) {
        case PARAM_REG_A:
          CpuGetRegisters()->a = b;
          break;
        case PARAM_REG_PC:
          CpuGetRegisters()->pc = w;
          g_nDisasmCurAddress = CpuGetRegisters()->pc;
          DisasmCalcTopBotAddress();
          break;
        case PARAM_REG_SP:
          CpuGetRegisters()->sp = b | 0x100;
          break;
        case PARAM_REG_X:
          CpuGetRegisters()->x = b;
          break;
        case PARAM_REG_Y:
          CpuGetRegisters()->y = b;
          break;
        default:
          return Help_Arg_1(CMD_REGISTER_SET);
      }
    }
  }

  //  g_nDisasmCurAddress = CpuGetRegisters()->pc;
  //  DisasmCalcTopBotAddress();

  return UPDATE_ALL;  // 1
}

//===========================================================================
void OutputTraceLine() {
#ifdef TODO  // Not supported for Linux yet
  DisasmLine_t line;
  GetDisassemblyLine(CpuGetRegisters()->pc, line);

  char sDisassembly[CONSOLE_WIDTH];  // DrawDisassemblyLine(
                                     // 0,CpuGetRegisters()->pc, sDisassembly);
                                     // // Get Disasm String
  FormatDisassemblyLine(line, sDisassembly, CONSOLE_WIDTH);

  char sFlags[_6502_NUM_FLAGS + 1];
  DrawFlags(0, CpuGetRegisters()->ps, sFlags);  // Get Flags String

  if (!g_hTraceFile) return;

  if (g_bTraceHeader) {
    g_bTraceHeader = false;

    if (g_bTraceFileWithVideoScanner) {
      fprintf(g_hTraceFile,
              //        "0000 0000 0000 00   00 00 00 0000 --------  0000:90 90
              //        90  NOP"
              "Vert Horz Addr Data A: X: Y: SP:  Flags     Addr:Opcode    "
              "Mnemonic\n");
    } else {
      fprintf(g_hTraceFile,
              //        "00 00 00 0000 --------  0000:90 90 90  NOP"
              "A: X: Y: SP:  Flags     Addr:Opcode    Mnemonic\n");
    }
  }

  char sTarget[16];
  if (line.bTargetValue) {
    sprintf(sTarget, "%s:%s", line.sTargetPointer, line.sTargetValue);
  }

  if (g_bTraceFileWithVideoScanner) {
    uint16_t addr = NTSC_VideoGetScannerAddressForDebugger();
    uint8_t data = mem[addr];

    fprintf(g_hTraceFile, "%04X %04X %04X   %02X %02X %02X %02X %04X %s  %s\n",
            g_nVideoClockVert, g_nVideoClockHorz, addr, data,
            (unsigned)CpuGetRegisters()->a, (unsigned)CpuGetRegisters()->x,
            (unsigned)CpuGetRegisters()->y, (unsigned)CpuGetRegisters()->sp,
            (char*)sFlags, sDisassembly
            //, sTarget // TODO: Show target?
    );
  } else {
    fprintf(g_hTraceFile, "%02X %02X %02X %04X %s  %s\n",
            (unsigned)CpuGetRegisters()->a, (unsigned)CpuGetRegisters()->x,
            (unsigned)CpuGetRegisters()->y, (unsigned)CpuGetRegisters()->sp,
            (char*)sFlags, sDisassembly
            //, sTarget // TODO: Show target?
    );
  }
#endif
}

static void CheckBreakOpcode(int iOpcode) {
  if (iOpcode == 0x00) {  // BRK
    IsDebugBreakOnInvalid(AM_IMPLIED);
  }

  if (g_aOpcodes[iOpcode].sMnemonic[0] >=
      'a')  // All 6502/65C02 undocumented opcodes mnemonics are lowercase
            // strings!
  {
    // TODO: Translate g_aOpcodes[iOpcode].nAddressMode into {AM_1, AM_2, AM_3}
    IsDebugBreakOnInvalid(AM_1);
  }

  // User wants to enter debugger on specific opcode? (NB. Can't be BRK)
  if (g_iDebugBreakOnOpcode && g_iDebugBreakOnOpcode == iOpcode) {
    g_bDebugBreakpointHit |= BP_HIT_OPCODE;
  }
}

void DebugContinueStepping(const bool bCallerWillUpdateDisplay) {
  static bool bForceSingleStepNext =
      false;  // Allow at least one instruction to execute so we don't trigger
              // on the same invalid opcode

  if (g_nDebugSkipLen > 0) {
    if ((CpuGetRegisters()->pc >= g_nDebugSkipStart) &&
        (CpuGetRegisters()->pc < (g_nDebugSkipStart + g_nDebugSkipLen))) {
      // Enter turbo debugger mode -- UI not updated, etc.
      g_nDebugSteps = -1;
      g_state.mode = MODE_STEPPING;
    } else {
      // Enter normal debugger mode -- UI updated every instruction, etc.
      g_nDebugSteps = 1;
      g_state.mode = MODE_STEPPING;
    }
  }

  bool bDoSingleStep = true;

  if ((g_nDebugSteps != 0) || (bForceSingleStepNext)) {
    if (!bForceSingleStepNext) {
      if (g_hTraceFile) {
        OutputTraceLine();
      }

      g_bDebugBreakpointHit = BP_HIT_NONE;

      if (MemIsAddrCodeMemory(CpuGetRegisters()->pc)) {
        uint8_t nOpcode = *(mem + CpuGetRegisters()->pc);

        // Update profiling stats
        int nOpmode = g_aOpcodes[nOpcode].nAddressMode;
        g_aProfileOpcodes[nOpcode].m_nCount++;
        g_aProfileOpmodes[nOpmode].m_nCount++;

        CheckBreakOpcode(nOpcode);  // Can set g_bDebugBreakpointHit
      } else {
        g_bDebugBreakpointHit = BP_HIT_PC_READ_FLOATING_BUS_OR_IO_MEM;
      }

      if (g_bDebugBreakpointHit) {
        bDoSingleStep = false;
        bForceSingleStepNext =
            true;  // Allow next single-step (after this) to execute
      }
    }

    if (bDoSingleStep) {
      if (g_nDebugSteps > 0) {
        g_nDebugSteps--;
      }

      bForceSingleStepNext = false;

      // Single-step the CPU
      if (g_state.mode == MODE_DEBUG) {
        g_state.mode = MODE_STEPPING;
      }

      CPU_Step();
    }
  }

  if ((g_nDebugSteps == 0) && (!bForceSingleStepNext)) {
    g_state.mode = MODE_DEBUG;
    g_nDebugSteps = 0;

    DisasmCalcTopBotAddress();

    if (!bCallerWillUpdateDisplay) {
      UpdateDisplay(UPDATE_ALL);
    }
  }
}

void DebugStopStepping() {
  assert(g_state.mode == MODE_STEPPING);

  if (g_state.mode != MODE_STEPPING) {
    return;
  }

  g_nDebugSteps = 0;  // On next DebugContinueStepping(), stop single-stepping
                      // and transition to MODE_DEBUG
  ClearTempBreakpoints();
}

// Output
// _________________________________________________________________________________________
