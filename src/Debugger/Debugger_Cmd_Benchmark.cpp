#include "Debugger_Cmd_Benchmark.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Cmd_CPU.h"
#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
extern void frame_refresh_status(int);
#include <cstdio>
#include <cstring>

#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Help.h"
#include "Debugger_Parser.h"

// Globals originally from Debug.cpp
bool g_benchmarking = false;
bool g_profiling = false;

ProfileOpcode_t g_profile_opcodes[NUM_OPCODES];
ProfileOpmode_t g_profile_opmodes[NUM_OPMODES];
uint64_t g_profile_begin_cycles = 0;  // g_cumulative_cycles // PROFILE RESET

const std::string g_file_name_profile = "Profile.txt";
int g_profile_line_count = 0;
char g_profile_line[NUM_PROFILE_LINES][CONSOLE_WIDTH] = {};

uint32_t extbench = 0;

// Externs
extern uint16_t g_disasm_cur_address;
extern uint16_t g_disasm_top_address;
extern uint16_t g_disasm_bot_address;
extern int g_disasm_cur_line;
extern bool g_disasm_cur_bad;

// Implementation ___________________________________________________________

auto CmdBenchmarkStart(int nArgs) -> Update_t {
  (void)nArgs;
  g_benchmarking = true;
  extbench = 0;
  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdBenchmark(int nArgs) -> Update_t {
  if (!nArgs) {
    g_benchmarking = false;
  } else {
    g_benchmarking = true;
    extbench = 0;
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdProfileList(int nArgs) -> Update_t;

auto CmdProfile(int nArgs) -> Update_t {
  if (!nArgs) {
    return CmdProfileList(0);
  }

  int iArg = 1;
  int iParam = 0;
  bool bFound = FindParam(g_args[iArg].sArg, MATCH_EXACT, iParam,
                          _PARAM_PROFILE_BEGIN, _PARAM_PROFILE_END) > 0;

  if (bFound) {
    if (iParam == PARAM_PROFILE_RESET) {
      ProfileReset();
    } else if (iParam == PARAM_PROFILE_SAVE) {
      if (ProfileSave()) {
        char sText[CONSOLE_WIDTH];
        ConsoleBufferPushFormat(sText, " Saved: %s",
                                g_file_name_profile.c_str());
      }
    } else if (iParam == PARAM_PROFILE_LIST) {
      return CmdProfileList(0);
    } else {
      g_profiling = (iParam == PARAM_PROFILE_ON);
      g_profile_begin_cycles = g_cumulative_cycles;
    }
  } else {
    return Help_Arg_1(CMD_PROFILE);
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto ProfileLinePeek(int iLine) -> char* {
  char* text = nullptr;

  if (iLine < 0) {
    iLine = 0;
  }

  if (iLine <= g_profile_line_count) {
    text = &g_profile_line[iLine][0];
  }

  return text;
}

void ProfileReset() {
  int opcode = 0;
  for (opcode = 0; opcode < NUM_OPCODES; opcode++) {
    g_profile_opcodes[opcode].opcode = opcode;
    g_profile_opcodes[opcode].count = 0;
  }

  int iOpmode = 0;
  for (iOpmode = 0; iOpmode < NUM_OPMODES; iOpmode++) {
    g_profile_opmodes[iOpmode].opmode = iOpmode;
    g_profile_opmodes[iOpmode].count = 0;
  }

  g_profile_line_count = 0;
  g_profile_begin_cycles = g_cumulative_cycles;
}

void ProfileFormat(bool bSeperateColumns, int eFormatMode) {
  (void)bSeperateColumns;
  (void)eFormatMode;
  int opcode = 0;
  int iOpmode = 0;

  bool bOpcodeGood = true;
  bool bOpmodeGood = true;

  std::vector<ProfileOpcode_t> vProfileOpcode(&g_profile_opcodes[0],
                                              &g_profile_opcodes[NUM_OPCODES]);
  std::vector<ProfileOpmode_t> vProfileOpmode(&g_profile_opmodes[0],
                                              &g_profile_opmodes[NUM_OPMODES]);

  // sort >
  std::sort(vProfileOpcode.begin(), vProfileOpcode.end(), ProfileOpcode_t());
  std::sort(vProfileOpmode.begin(), vProfileOpmode.end(), ProfileOpmode_t());

  g_profile_line_count = 0;
  char* text = &g_profile_line[0][0];

  uint64_t nTotalCycles = g_cumulative_cycles - g_profile_begin_cycles;
  sprintf(text, "Cycles: %llu\n",
          static_cast<unsigned long long>(nTotalCycles));
  g_profile_line_count++;

  while (bOpcodeGood || bOpmodeGood) {
    text = &g_profile_line[g_profile_line_count][0];
    text[0] = 0;

    if (opcode < NUM_OPCODES) {
      if (vProfileOpcode.at(static_cast<size_t>(opcode)).count > 0) {
        sprintf(text, "%s: %llu",
                g_opcodes65_c02[vProfileOpcode.at(static_cast<size_t>(opcode))
                                    .opcode]
                    .sMnemonic,
                static_cast<unsigned long long>(
                    vProfileOpcode.at(static_cast<size_t>(opcode)).count));
      } else {
        bOpcodeGood = false;
      }
    }

    if (iOpmode < NUM_OPMODES) {
      if (vProfileOpmode.at(static_cast<size_t>(iOpmode)).count > 0) {
        char sOpmode[CONSOLE_WIDTH];
        sprintf(sOpmode, "  %s: %llu",
                g_opmodes[static_cast<size_t>(
                              vProfileOpmode.at(static_cast<size_t>(iOpmode))
                                  .opmode)]
                    .name,
                static_cast<unsigned long long>(
                    vProfileOpmode.at(static_cast<size_t>(iOpmode)).count));
        strcat(text, sOpmode);
      } else {
        bOpmodeGood = false;
      }
    }

    if (text[0]) {
      strcat(text, "\n");
      g_profile_line_count++;
    }

    opcode++;
    iOpmode++;

    if (g_profile_line_count >= (NUM_PROFILE_LINES - 1)) {
      break;
    }
  }
}

auto CmdProfileList(int nArgs) -> Update_t {
  (void)nArgs;
  ProfileFormat(true, 0);

  int nLines = MIN(g_profile_line_count, g_console_display_lines - 1);
  return ConsoleBufferTryUnpause(nLines);
}

auto ProfileSave() -> bool {
  bool bStatus = false;
  FilePtr_t hFile(fopen(g_file_name_profile.c_str(), "w"), fclose);

  if (hFile) {
    ProfileFormat(true, 0);

    char* text = nullptr;
    int nLine = g_profile_line_count;
    int iLine = 0;

    for (iLine = 0; iLine < nLine; iLine++) {
      text = ProfileLinePeek(iLine);
      if (text) {
        fputs(text, hFile.get());
      }
    }

    bStatus = true;
  }

  return bStatus;
}
