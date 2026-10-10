// SPDX-License-Identifier: GPL-2.0-only
#include "Debugger_Cmd_CPU.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/Frontend.h"

// Definitions
int debug_steps = 0;
uint32_t debug_step_cycles = 0;
int debug_step_start = 0;
int debug_step_until = -1;
int debug_skip_start = 0;
int debug_skip_len = 0;

bool debug_full_speed = false;
bool last_go_cmd_was_full_speed = false;
bool go_cmd_reinit_flag = false;

FilePtr trace_file{nullptr, fclose};
bool trace_header = false;
bool trace_file_with_video_scanner = false;
char file_name_trace[] = "Trace.txt";

extern uint32_t video_clock_horz;
extern uint32_t video_clock_vert;

// Implementation
// CPU
// ____________________________________________________________________________________________
// CPU Step, Trace
// ________________________________________________________________________________

//===========================================================================
auto CmdGo(int nArgs, const bool bFullSpeed) -> UpdateResult {
  // G StopAddress [SkipAddress,Length]
  // Example:
  //  G C600 FA00,FFFF
  // TODO: G addr1,len   addr3,len
  // TODO: G addr1:addr2 addr3:addr4

  const int kCmdGo = !bFullSpeed ? CMD_GO_NORMAL_SPEED : CMD_GO_FULL_SPEED;

  debug_steps = -1;
  debug_step_cycles = 0;
  debug_step_start = cpu_get_registers()->pc;
  debug_step_until = (nArgs != 0) ? args[1].nValue : -1;
  debug_skip_start = -1;
  debug_skip_len = -1;

  if (nArgs > 4) {
    return Help_Arg_1(kCmdGo);
  }

  //     G StopAddress [SkipAddress,Len]
  // Old   1            2           2
  //     G addr addr [, len]
  // New   1    2     3 4
  if (nArgs > 1) {
    int iArg = 2;
    debug_skip_start = args[iArg].nValue;

    int nLen = 0;
    int nEnd = 0;

    if (nArgs > 2) {
      if (args[iArg + 1].eToken == TOKEN_COMMA) {
        if (nArgs > 3) {
          nLen = args[iArg + 2].nValue;
          nEnd = debug_skip_start + nLen;
          if (nEnd > static_cast<int>(apple2_6502_mem_end)) {
            nEnd = apple2_6502_mem_end + 1;
          }
        } else {
          return Help_Arg_1(kCmdGo);
        }
      } else if (args[iArg + 1].eToken == TOKEN_COLON) {
        nEnd = args[iArg + 2].nValue + 1;
      } else {
        return Help_Arg_1(kCmdGo);
      }
    } else {
      return Help_Arg_1(kCmdGo);
    }

    nLen = nEnd - debug_skip_start;
    if (nLen < 0) {
      nLen = -nLen;
    }
    debug_skip_len = nLen;
    debug_skip_len &= apple2_6502_mem_end;
  }

  //  uint16_t nAddressSymbol = 0;
  //  bool bFoundSymbol = FindAddressFromSymbol( args[1].sArg, &
  //  nAddressSymbol ); if (bFoundSymbol)
  //    debug_step_until = nAddressSymbol;

  //  if (!debug_step_until)
  //    debug_step_until = GetAddress(args[1].sArg);

  debugger_eat_key = true;

  debug_full_speed = bFullSpeed;
  last_go_cmd_was_full_speed = bFullSpeed;
  go_cmd_reinit_flag = true;

  system_state.mode = app_mode_stepping;
  frame_refresh_status(draw_title);

  audio_mixer_set_fade(fade_in);

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdGoNormalSpeed(int nArgs) -> UpdateResult { return CmdGo(nArgs, false); }

auto CmdGoFullSpeed(int nArgs) -> UpdateResult { return CmdGo(nArgs, true); }

auto CmdBreakInvalid(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    debug_break_on_invalid ^= 1;
  } else {
    debug_break_on_invalid = static_cast<int>(args[1].nValue != 0);
  }
  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdBreakOpcode(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    debug_break_on_opcode = 0;
  } else {
    debug_break_on_opcode = args[1].nValue & 0xFF;
  }
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdStackPop(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdStackPopPseudo(int nArgs) -> UpdateResult {
  (void)nArgs;
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdStepOver(int nArgs) -> UpdateResult {
  // assert( disasm_cur_address == cpu_get_registers()->pc );

  //  debug_steps = nArgs ? args[1].nValue : 1;
  uint16_t nDebugSteps = (nArgs != 0) ? args[1].nValue : 1;

  while (nDebugSteps-- > 0) {
    int nOpcode = *(mem + cpu_get_registers()->pc);  // disasm_cur_address
    //  int eMode = opcodes[ nOpcode ].addrmode;
    //  int nByte = opmodes[eMode].bytes;
    //  if ((eMode ==  AM_A) &&

    CmdTrace(0);
    if (nOpcode == OPCODE_JSR) {
      CmdStepOut(0);
      debug_steps = 0xFFFF;
      while (debug_steps != 0) {
        DebugContinueStepping(true);
      }
    }
  }

  return UPDATE_ALL;
}

//===========================================================================
auto CmdStepOut(int nArgs) -> UpdateResult {
  (void)nArgs;
  // TODO: "RET" should probably pop the Call stack
  // Also see: CmdCursorJumpRetAddr
  uint16_t address = 0;
  if (GetStackReturnAddress(address)) {
    nArgs = Arg_1(address);
    args[1].sArg[0] = 0;
    CmdGo(1, true);
  }

  return UPDATE_ALL;
}

//===========================================================================
auto CmdTrace(int nArgs) -> UpdateResult {
  debug_steps = (nArgs != 0) ? args[1].nValue : 1;
  debug_step_cycles = 0;
  debug_step_start = cpu_get_registers()->pc;
  debug_step_until = -1;
  system_state.mode = app_mode_stepping;
  frame_refresh_status(draw_title);
  DebugContinueStepping(true);

  return UPDATE_ALL;  // TODO: Verify // 0
}

//===========================================================================
auto CmdTraceFile(int nArgs) -> UpdateResult {
  char sText[CONSOLE_WIDTH] = "";

  if (trace_file) {
    trace_file.reset();

    ConsoleBufferPush("Trace stopped.");
  } else {
    std::string sFileName;

    if (nArgs != 0) {
      sFileName = args[1].sArg;
    } else {
      sFileName = file_name_trace;
    }

    trace_file_with_video_scanner = (nArgs >= 2);

    const std::string sFilePath =
        std::string(system_state.current_dir.data()) + sFileName;

    trace_file.reset(fopen(sFilePath.c_str(), "wt"));

    if (trace_file) {
      const char* pTextHdr = trace_file_with_video_scanner
                                 ? "Trace (with video info) started: %s"
                                 : "Trace started: %s";
      ConsoleBufferPushFormat(sText, pTextHdr, sFilePath.c_str());
      trace_header = true;
    } else {
      ConsoleBufferPushFormat(sText, "Trace ERROR: %s", sFilePath.c_str());
    }
  }

  ConsoleBufferToDisplay();

  return UPDATE_ALL;  // TODO: Verify // 0
}

//===========================================================================
auto CmdTraceLine(int nArgs) -> UpdateResult {
  debug_steps = (nArgs != 0) ? args[1].nValue : 1;
  debug_step_cycles = 1;
  debug_step_start = cpu_get_registers()->pc;
  debug_step_until = -1;

  system_state.mode = app_mode_stepping;
  frame_refresh_status(draw_title);
  DebugContinueStepping(true);

  return UPDATE_ALL;  // TODO: Verify // 0
}

// Unassemble
//===========================================================================
auto CmdUnassemble(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    return Help_Arg_1(CMD_UNASSEMBLE);
  }

  uint16_t address = args[1].nValue;
  disasm_top_address = address;

  DisasmCalcCurFromTopAddress();
  DisasmCalcBotFromTopAddress();

  return UPDATE_DISASM;
}

//===========================================================================
auto CmdKey(int nArgs) -> UpdateResult {
  uint8_t code = static_cast<uint8_t>(' ');
  if (nArgs != 0) {
    code = (args[1].nValue != 0) ? static_cast<uint8_t>(args[1].nValue)
                                   : static_cast<uint8_t>(args[1].sArg[0]);
  }

  linapple_set_key_state(code, true);
  linapple_set_key_state(code, false);

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdIn(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    return Help_Arg_1(CMD_IN);
  }

  uint16_t address = args[1].nValue;

  io_map_dispatch(cpu_get_registers()->pc, address & 0xFFFF, 0, 0, 0);

  return UPDATE_CONSOLE_DISPLAY;  // TODO: Verify // 1
}

//===========================================================================
auto CmdJSR(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    return Help_Arg_1(CMD_JSR);
  }

  uint16_t address = args[1].nValue & apple2_6502_mem_end;

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
auto CmdNOP(int nArgs) -> UpdateResult {
  (void)nArgs;
  int opcode = 0;
  int iOpmode = 0;
  int nOpbytes = 0;

  GetOpcodeOpmodeOpbyte(opcode, iOpmode, nOpbytes);

  while ((nOpbytes--) != 0) {
    *(mem + cpu_get_registers()->pc + nOpbytes) = 0xEA;
  }

  return UPDATE_ALL;
}

//===========================================================================
auto CmdOut(int nArgs) -> UpdateResult {
  //  if ((!nArgs) ||
  //      ((args[1].sArg[0] != '0') && (!args[1].nValue) &&
  //      (!GetAddress(args[1].sArg))))
  //     return DisplayHelp(CmdInput);

  if (nArgs == 0) {
    Help_Arg_1(CMD_OUT);
  }

  uint16_t address = args[1].nValue;

  IOWrite[(address >> 4) & 0xF](cpu_get_registers()->pc, address & 0xFF, 1,
                                args[2].nValue & 0xFF, 0);

  return UPDATE_ALL;
}

auto CmdRegisterSet(int nArgs) -> UpdateResult {
  if (nArgs < 2)  // || ((args[2].sArg[0] != '0') && !args[2].nValue))
  {
    return Help_Arg_1(CMD_REGISTER_SET);
  }

  char* pName = args[1].sArg;
  int iParam = 0;
  if (FindParam(pName, MATCH_EXACT, iParam, PARAM_REGS_BEGIN, PARAM_REGS_END) !=
      0) {
    int iArg = 2;
    if (args[iArg].eToken == TOKEN_EQUAL) {
      iArg++;
    }

    if (iArg > nArgs) {
      return Help_Arg_1(CMD_REGISTER_SET);
    }

    auto b = static_cast<uint8_t>(args[iArg].nValue & 0xFF);
    auto w = static_cast<uint16_t>(args[iArg].nValue & 0xFFFF);

    switch (iParam) {
      case PARAM_REG_A:
        cpu_get_registers()->a = b;
        break;
      case PARAM_REG_PC:
        cpu_get_registers()->pc = w;
        disasm_cur_address = cpu_get_registers()->pc;
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

  //  disasm_cur_address = cpu_get_registers()->pc;
  //  DisasmCalcTopBotAddress();

  return UPDATE_ALL;  // 1
}

//===========================================================================
auto OutputTraceLine() -> void {}

static auto CheckBreakOpcode(int opcode) -> void {
  if (opcode == 0x00) {  // BRK
    IsDebugBreakOnInvalid(AM_IMPLIED);
  }

  if (opcodes[opcode].sMnemonic[0] >=
      'a')  // All 6502/65C02 undocumented opcodes mnemonics are lowercase
            // strings!
  {
    // TODO: Translate opcodes[opcode].nAddressMode into {AM_1, AM_2, AM_3}
    IsDebugBreakOnInvalid(AM_1);
  }

  // User wants to enter debugger on specific opcode? (NB. Can't be BRK)
  if ((debug_break_on_opcode != 0) && debug_break_on_opcode == opcode) {
    debug_breakpoint_hit |= BP_HIT_OPCODE;
  }
}

auto DebugContinueStepping(const bool bCallerWillUpdateDisplay) -> void {
  static bool bForceSingleStepNext =
      false;  // Allow at least one instruction to execute so we don't trigger
              // on the same invalid opcode

  if (debug_skip_len > 0) {
    if ((cpu_get_registers()->pc >= debug_skip_start) &&
        (cpu_get_registers()->pc < (debug_skip_start + debug_skip_len))) {
      // Enter turbo debugger mode -- UI not updated, etc.
      debug_steps = -1;
      system_state.mode = app_mode_stepping;
    } else {
      // Enter normal debugger mode -- UI updated every instruction, etc.
      debug_steps = 1;
      system_state.mode = app_mode_stepping;
    }
  }

  bool bDoSingleStep = true;

  if ((debug_steps != 0) || bForceSingleStepNext) {
    if (!bForceSingleStepNext) {
      if (trace_file) {
        OutputTraceLine();
      }

      debug_breakpoint_hit = BP_HIT_NONE;

      if (mem_is_addr_code_memory(cpu_get_registers()->pc)) {
        uint8_t nOpcode = *(mem + cpu_get_registers()->pc);

        // Update profiling stats
        int nOpmode = opcodes[nOpcode].nAddressMode;
        profile_opcodes[nOpcode].count++;
        profile_opmodes[nOpmode].count++;

        CheckBreakOpcode(nOpcode);  // Can set debug_breakpoint_hit
      } else {
        debug_breakpoint_hit = BP_HIT_PC_READ_FLOATING_BUS_OR_IO_MEM;
      }

      if (debug_breakpoint_hit != 0) {
        bDoSingleStep = false;
        bForceSingleStepNext =
            true;  // Allow next single-step (after this) to execute
      }
    }

    if (bDoSingleStep) {
      if (debug_steps > 0) {
        debug_steps--;
      }

      bForceSingleStepNext = false;

      // Single-step the CPU
      if (system_state.mode == app_mode_debug) {
        system_state.mode = app_mode_stepping;
      }

      cpu_step();
    }
  }

  if ((debug_steps == 0) && (!bForceSingleStepNext)) {
    system_state.mode = app_mode_debug;
    debug_steps = 0;

    DisasmCalcTopBotAddress();

    if (!bCallerWillUpdateDisplay) {
      UpdateDisplay(UPDATE_ALL);
    }
  }
}

auto DebugStopStepping() -> void {
  assert(system_state.mode == app_mode_stepping);

  if (system_state.mode != app_mode_stepping) {
    return;
  }

  debug_steps = 0;  // On next DebugContinueStepping(), stop single-stepping
                      // and transition to app_mode_debug
  ClearTempBreakpoints();
}

// Output
// _________________________________________________________________________________________
