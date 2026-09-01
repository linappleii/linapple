#include "Debugger_Cmd_CPU.h"

#include <cstdint>
#include <string>

#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Types.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "core/LinAppleCore.h"
extern void frame_refresh_status(int);
#include <cassert>
#include <cstdio>

#include "Debugger_Assembler.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Display.h"
#include "Debugger_Parser.h"
#include "Debugger_Symbols.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/AudioMixer.h"
#include "core/Peripheral.h"

// Definitions
int g_debug_steps = 0;
uint32_t g_debug_step_cycles = 0;
int g_debug_step_start = 0;
int g_debug_step_until = -1;
int g_debug_skip_start = 0;
int g_debug_skip_len = 0;

bool g_debug_full_speed = false;
bool g_last_go_cmd_was_full_speed = false;
bool g_go_cmd_reinit_flag = false;

FILE* g_trace_file = nullptr;
bool g_trace_header = false;
bool g_trace_file_with_video_scanner = false;
char g_file_name_trace[] = "Trace.txt";

extern uint16_t g_disasm_cur_address;
extern int g_disasm_cur_line;

extern ProfileOpcode_t g_profile_opcodes[NUM_OPCODES];
extern ProfileOpmode_t g_profile_opmodes[NUM_OPMODES];

extern int g_debug_break_on_opcode;
extern int g_debug_breakpoint_hit;
extern int g_debug_break_on_invalid;

extern uint16_t g_disasm_top_address;
extern uint16_t g_disasm_bot_address;

extern uint32_t g_video_clock_horz;
extern uint32_t g_video_clock_vert;

extern VideoScannerDisplayInfo_t g_video_scanner_display_info;

void DisasmCalcTopBotAddress();
auto IsDebugBreakOnInvalid(int iOpcodeType) -> bool;
auto NTSC_VideoGetScannerAddressForDebugger() -> uint16_t;
void video_refresh_screen(int bVideoModeFlags, bool bForceRedraw);
auto video_get_sw_page2() -> bool;
auto video_get_sw_mixed() -> bool;

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

  g_debug_steps = -1;
  g_debug_step_cycles = 0;
  g_debug_step_start = cpu_get_registers()->pc;
  g_debug_step_until = nArgs ? g_args[1].nValue : -1;
  g_debug_skip_start = -1;
  g_debug_skip_len = -1;

  if (nArgs > 4) {
    return Help_Arg_1(kCmdGo);
  }

  //     G StopAddress [SkipAddress,Len]
  // Old   1            2           2
  //     G addr addr [, len]
  // New   1    2     3 4
  if (nArgs > 1) {
    int iArg = 2;
    g_debug_skip_start = g_args[iArg].nValue;

#if DEBUG_VAL_2
    uint16_t address = g_args[iArg].nVal2;
#endif
    int nLen = 0;
    int nEnd = 0;

    if (nArgs > 2) {
      if (g_args[iArg + 1].eToken == TOKEN_COMMA) {
        if (nArgs > 3) {
          nLen = g_args[iArg + 2].nValue;
          nEnd = g_debug_skip_start + nLen;
          if (nEnd > static_cast<int>(_6502_MEM_END)) {
            nEnd = _6502_MEM_END + 1;
          }
        } else {
          return Help_Arg_1(kCmdGo);
        }
      } else if (g_args[iArg + 1].eToken == TOKEN_COLON) {
        nEnd = g_args[iArg + 2].nValue + 1;
      } else {
        return Help_Arg_1(kCmdGo);
      }
    } else {
      return Help_Arg_1(kCmdGo);
    }

    nLen = nEnd - g_debug_skip_start;
    if (nLen < 0) {
      nLen = -nLen;
    }
    g_debug_skip_len = nLen;
    g_debug_skip_len &= _6502_MEM_END;

#if _DEBUG
    char sText[CONSOLE_WIDTH];
    ConsoleBufferPushFormat(sText, "Start: %04X,%04X  End: %04X  Len: %04X",
                            g_debug_skip_start, g_debug_skip_len, nEnd, nLen);
    ConsoleBufferToDisplay();
#endif
  }

  //  uint16_t nAddressSymbol = 0;
  //  bool bFoundSymbol = FindAddressFromSymbol( g_args[1].sArg, &
  //  nAddressSymbol ); if (bFoundSymbol)
  //    g_debug_step_until = nAddressSymbol;

  //  if (!g_debug_step_until)
  //    g_debug_step_until = GetAddress(g_args[1].sArg);

  g_debugger_eat_key = true;

  g_debug_full_speed = bFullSpeed;
  g_last_go_cmd_was_full_speed = bFullSpeed;
  g_go_cmd_reinit_flag = true;

  g_state.mode = MODE_STEPPING;
  frame_refresh_status(DRAW_TITLE);

  audio_mixer_set_fade(fade_in);

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdGoNormalSpeed(int nArgs) -> Update_t { return CmdGo(nArgs, false); }

auto CmdGoFullSpeed(int nArgs) -> Update_t { return CmdGo(nArgs, true); }

auto CmdBreakInvalid(int nArgs) -> Update_t {
  if (nArgs == 0) {
    g_debug_break_on_invalid ^= 1;
  } else {
    g_debug_break_on_invalid = g_args[1].nValue != 0;
  }
  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdBreakOpcode(int nArgs) -> Update_t {
  if (nArgs == 0) {
    g_debug_break_on_opcode = 0;
  } else {
    g_debug_break_on_opcode = g_args[1].nValue & 0xFF;
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
  // assert( g_disasm_cur_address == cpu_get_registers()->pc );

  //  g_debug_steps = nArgs ? g_args[1].nValue : 1;
  uint16_t nDebugSteps = nArgs ? g_args[1].nValue : 1;

  while (nDebugSteps-- > 0) {
    int nOpcode = *(mem + cpu_get_registers()->pc);  // g_disasm_cur_address
    //  int eMode = g_opcodes[ nOpcode ].addrmode;
    //  int nByte = g_opmodes[eMode]._nBytes;
    //  if ((eMode ==  AM_A) &&

    CmdTrace(0);
    if (nOpcode == OPCODE_JSR) {
      CmdStepOut(0);
      g_debug_steps = 0xFFFF;
      while (g_debug_steps != 0) {
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
  uint16_t address = 0;
  if (_6502_GetStackReturnAddress(address)) {
    nArgs = _Arg_1(address);
    g_args[1].sArg[0] = 0;
    CmdGo(1, true);
  }

  return UPDATE_ALL;
}

//===========================================================================
auto CmdTrace(int nArgs) -> Update_t {
  g_debug_steps = nArgs ? g_args[1].nValue : 1;
  g_debug_step_cycles = 0;
  g_debug_step_start = cpu_get_registers()->pc;
  g_debug_step_until = -1;
  g_state.mode = MODE_STEPPING;
  frame_refresh_status(DRAW_TITLE);
  DebugContinueStepping(true);

  return UPDATE_ALL;  // TODO: Verify // 0
}

//===========================================================================
auto CmdTraceFile(int nArgs) -> Update_t {
  char sText[CONSOLE_WIDTH] = "";

  if (g_trace_file) {
    fclose(g_trace_file);
    g_trace_file = nullptr;

    ConsoleBufferPush("Trace stopped.");
  } else {
    std::string sFileName;

    if (nArgs) {
      sFileName = g_args[1].sArg;
    } else {
      sFileName = g_file_name_trace;
    }

    g_trace_file_with_video_scanner = (nArgs >= 2);

    const std::string sFilePath =
        std::string(g_state.current_dir.data()) + sFileName;

    g_trace_file = fopen(sFilePath.c_str(), "wt");

    if (g_trace_file) {
      const char* pTextHdr = g_trace_file_with_video_scanner
                                 ? "Trace (with video info) started: %s"
                                 : "Trace started: %s";
      ConsoleBufferPushFormat(sText, pTextHdr, sFilePath.c_str());
      g_trace_header = true;
    } else {
      ConsoleBufferPushFormat(sText, "Trace ERROR: %s", sFilePath.c_str());
    }
  }

  ConsoleBufferToDisplay();

  return UPDATE_ALL;  // TODO: Verify // 0
}

//===========================================================================
auto CmdTraceLine(int nArgs) -> Update_t {
  g_debug_steps = nArgs ? g_args[1].nValue : 1;
  g_debug_step_cycles = 1;
  g_debug_step_start = cpu_get_registers()->pc;
  g_debug_step_until = -1;

  g_state.mode = MODE_STEPPING;
  frame_refresh_status(DRAW_TITLE);
  DebugContinueStepping(true);

  return UPDATE_ALL;  // TODO: Verify // 0
}

// Unassemble
//===========================================================================
auto CmdUnassemble(int nArgs) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(CMD_UNASSEMBLE);
  }

  uint16_t address = g_args[1].nValue;
  g_disasm_top_address = address;

  DisasmCalcCurFromTopAddress();
  DisasmCalcBotFromTopAddress();

  return UPDATE_DISASM;
}

//===========================================================================
auto CmdKey(int nArgs) -> Update_t {
  uint8_t code =
      nArgs ? (g_args[1].nValue ? static_cast<uint8_t>(g_args[1].nValue)
                                : static_cast<uint8_t>(g_args[1].sArg[0]))
            : static_cast<uint8_t>(' ');

  // Send key-down event
  KeyboardEvent_t ev = {code, 1U, 0, 0, 0, 0, {0, 0, 0}};
  peripheral_command(0, keyboard_cmd_event, &ev, sizeof(ev));

  // Send key-up event immediately to simulate a momentary press
  ev.is_down = 0U;
  peripheral_command(0, keyboard_cmd_event, &ev, sizeof(ev));

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdIn(int nArgs) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(CMD_IN);
  }

  uint16_t address = g_args[1].nValue;

  io_map_dispatch(cpu_get_registers()->pc, address & 0xFFFF, 0, 0, 0);

  return UPDATE_CONSOLE_DISPLAY;  // TODO: Verify // 1
}

//===========================================================================
auto CmdJSR(int nArgs) -> Update_t {
  if (!nArgs) {
    return Help_Arg_1(CMD_JSR);
  }

  uint16_t address = g_args[1].nValue & _6502_MEM_END;

  // Mark Stack Page as dirty
  *(memdirty + (cpu_get_registers()->sp >> 8)) = 1;

  // Push PC onto stack
  *(mem + cpu_get_registers()->sp) = ((cpu_get_registers()->pc >> 8) & 0xFF);
  cpu_get_registers()->sp--;

  *(mem + cpu_get_registers()->sp) =
      ((cpu_get_registers()->pc >> 0) - 1) & 0xFF;
  cpu_get_registers()->sp--;

  // Jump to new address
  cpu_get_registers()->pc = address;

  return UPDATE_ALL;
}

//===========================================================================
auto CmdNOP(int nArgs) -> Update_t {
  (void)nArgs;
  int opcode = 0;
  int iOpmode = 0;
  int nOpbytes = 0;

  _6502_GetOpcodeOpmodeOpbyte(opcode, iOpmode, nOpbytes);

  while (nOpbytes--) {
    *(mem + cpu_get_registers()->pc + nOpbytes) = 0xEA;
  }

  return UPDATE_ALL;
}

//===========================================================================
auto CmdOut(int nArgs) -> Update_t {
  //  if ((!nArgs) ||
  //      ((g_args[1].sArg[0] != '0') && (!g_args[1].nValue) &&
  //      (!GetAddress(g_args[1].sArg))))
  //     return DisplayHelp(CmdInput);

  if (!nArgs) {
    Help_Arg_1(CMD_OUT);
  }

  uint16_t address = g_args[1].nValue;

  IOWrite[(address >> 4) & 0xF](cpu_get_registers()->pc, address & 0xFF, 1,
                                g_args[2].nValue & 0xFF, 0);

  return UPDATE_ALL;
}

auto CmdRegisterSet(int nArgs) -> Update_t {
  if (nArgs < 2)  // || ((g_args[2].sArg[0] != '0') && !g_args[2].nValue))
  {
    return Help_Arg_1(CMD_REGISTER_SET);
  } else {
    char* pName = g_args[1].sArg;
    int iParam = 0;
    if (FindParam(pName, MATCH_EXACT, iParam, _PARAM_REGS_BEGIN,
                  _PARAM_REGS_END)) {
      int iArg = 2;
      if (g_args[iArg].eToken == TOKEN_EQUAL) {
        iArg++;
      }

      if (iArg > nArgs) {
        return Help_Arg_1(CMD_REGISTER_SET);
      }

      auto b = static_cast<uint8_t>(g_args[iArg].nValue & 0xFF);
      auto w = static_cast<uint16_t>(g_args[iArg].nValue & 0xFFFF);

      switch (iParam) {
        case PARAM_REG_A:
          cpu_get_registers()->a = b;
          break;
        case PARAM_REG_PC:
          cpu_get_registers()->pc = w;
          g_disasm_cur_address = cpu_get_registers()->pc;
          DisasmCalcTopBotAddress();
          break;
        case PARAM_REG_SP:
          cpu_get_registers()->sp = b | 0x100;
          break;
        case PARAM_REG_X:
          cpu_get_registers()->x = b;
          break;
        case PARAM_REG_Y:
          cpu_get_registers()->y = b;
          break;
        default:
          return Help_Arg_1(CMD_REGISTER_SET);
      }
    }
  }

  //  g_disasm_cur_address = cpu_get_registers()->pc;
  //  DisasmCalcTopBotAddress();

  return UPDATE_ALL;  // 1
}

//===========================================================================
void OutputTraceLine() {
#ifdef TODO  // Not supported for Linux yet
  DisasmLine_t line;
  GetDisassemblyLine(cpu_get_registers()->pc, line);

  char
      sDisassembly[CONSOLE_WIDTH];  // DrawDisassemblyLine(
                                    // 0,cpu_get_registers()->pc, sDisassembly);
                                    // // Get Disasm String
  FormatDisassemblyLine(line, sDisassembly, CONSOLE_WIDTH);

  char sFlags[_6502_NUM_FLAGS + 1];
  DrawFlags(0, cpu_get_registers()->ps, sFlags);  // Get Flags String

  if (!g_trace_file) return;

  if (g_trace_header) {
    g_trace_header = false;

    if (g_trace_file_with_video_scanner) {
      fprintf(g_trace_file,
              //        "0000 0000 0000 00   00 00 00 0000 --------  0000:90 90
              //        90  NOP"
              "Vert Horz Addr Data A: X: Y: SP:  Flags     Addr:Opcode    "
              "Mnemonic\n");
    } else {
      fprintf(g_trace_file,
              //        "00 00 00 0000 --------  0000:90 90 90  NOP"
              "A: X: Y: SP:  Flags     Addr:Opcode    Mnemonic\n");
    }
  }

  char sTarget[16];
  if (line.bTargetValue) {
    sprintf(sTarget, "%s:%s", line.sTargetPointer, line.sTargetValue);
  }

  if (g_trace_file_with_video_scanner) {
    uint16_t addr = NTSC_VideoGetScannerAddressForDebugger();
    uint8_t data = mem[addr];

    fprintf(g_trace_file, "%04X %04X %04X   %02X %02X %02X %02X %04X %s  %s\n",
            g_video_clock_vert, g_video_clock_horz, addr, data,
            (unsigned)cpu_get_registers()->a, (unsigned)cpu_get_registers()->x,
            (unsigned)cpu_get_registers()->y, (unsigned)cpu_get_registers()->sp,
            (char*)sFlags, sDisassembly
            //, sTarget // TODO: Show target?
    );
  } else {
    fprintf(g_trace_file, "%02X %02X %02X %04X %s  %s\n",
            (unsigned)cpu_get_registers()->a, (unsigned)cpu_get_registers()->x,
            (unsigned)cpu_get_registers()->y, (unsigned)cpu_get_registers()->sp,
            (char*)sFlags, sDisassembly
            //, sTarget // TODO: Show target?
    );
  }
#endif
}

static void CheckBreakOpcode(int opcode) {
  if (opcode == 0x00) {  // BRK
    IsDebugBreakOnInvalid(AM_IMPLIED);
  }

  if (g_opcodes[opcode].sMnemonic[0] >=
      'a')  // All 6502/65C02 undocumented opcodes mnemonics are lowercase
            // strings!
  {
    // TODO: Translate g_opcodes[opcode].nAddressMode into {AM_1, AM_2, AM_3}
    IsDebugBreakOnInvalid(AM_1);
  }

  // User wants to enter debugger on specific opcode? (NB. Can't be BRK)
  if (g_debug_break_on_opcode && g_debug_break_on_opcode == opcode) {
    g_debug_breakpoint_hit |= BP_HIT_OPCODE;
  }
}

void DebugContinueStepping(const bool bCallerWillUpdateDisplay) {
  static bool bForceSingleStepNext =
      false;  // Allow at least one instruction to execute so we don't trigger
              // on the same invalid opcode

  if (g_debug_skip_len > 0) {
    if ((cpu_get_registers()->pc >= g_debug_skip_start) &&
        (cpu_get_registers()->pc < (g_debug_skip_start + g_debug_skip_len))) {
      // Enter turbo debugger mode -- UI not updated, etc.
      g_debug_steps = -1;
      g_state.mode = MODE_STEPPING;
    } else {
      // Enter normal debugger mode -- UI updated every instruction, etc.
      g_debug_steps = 1;
      g_state.mode = MODE_STEPPING;
    }
  }

  bool bDoSingleStep = true;

  if ((g_debug_steps != 0) || (bForceSingleStepNext)) {
    if (!bForceSingleStepNext) {
      if (g_trace_file) {
        OutputTraceLine();
      }

      g_debug_breakpoint_hit = BP_HIT_NONE;

      if (mem_is_addr_code_memory(cpu_get_registers()->pc)) {
        uint8_t nOpcode = *(mem + cpu_get_registers()->pc);

        // Update profiling stats
        int nOpmode = g_opcodes[nOpcode].nAddressMode;
        g_profile_opcodes[nOpcode].count++;
        g_profile_opmodes[nOpmode].count++;

        CheckBreakOpcode(nOpcode);  // Can set g_debug_breakpoint_hit
      } else {
        g_debug_breakpoint_hit = BP_HIT_PC_READ_FLOATING_BUS_OR_IO_MEM;
      }

      if (g_debug_breakpoint_hit) {
        bDoSingleStep = false;
        bForceSingleStepNext =
            true;  // Allow next single-step (after this) to execute
      }
    }

    if (bDoSingleStep) {
      if (g_debug_steps > 0) {
        g_debug_steps--;
      }

      bForceSingleStepNext = false;

      // Single-step the CPU
      if (g_state.mode == MODE_DEBUG) {
        g_state.mode = MODE_STEPPING;
      }

      cpu_step();
    }
  }

  if ((g_debug_steps == 0) && (!bForceSingleStepNext)) {
    g_state.mode = MODE_DEBUG;
    g_debug_steps = 0;

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

  g_debug_steps = 0;  // On next DebugContinueStepping(), stop single-stepping
                      // and transition to MODE_DEBUG
  ClearTempBreakpoints();
}

// Output
// _________________________________________________________________________________________
