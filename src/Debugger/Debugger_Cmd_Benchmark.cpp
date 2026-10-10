// SPDX-License-Identifier: GPL-2.0-only
#include "Debugger_Cmd_Benchmark.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"
#include "apple2/CPU.h"
#include "core/Util_Path.h"

// Globals originally from Debug.cpp
bool benchmarking = false;
bool profiling = false;

ProfileOpcode profile_opcodes[NUM_OPCODES];
ProfileOpmode profile_opmodes[NUM_OPMODES];
uint64_t profile_begin_cycles = 0;  // cumulative_cycles // PROFILE RESET

const char* const file_name_profile = "Profile.txt";
int profile_line_count = 0;
char profile_line[NUM_PROFILE_LINES][CONSOLE_WIDTH] = {};

uint32_t extbench = 0;

// Externs

// Implementation ___________________________________________________________

auto CmdBenchmarkStart(int nArgs) -> UpdateResult {
  (void)nArgs;
  benchmarking = true;
  extbench = 0;
  return UPDATE_CONSOLE_DISPLAY;
}

auto CmdBenchmark(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    benchmarking = false;
  } else {
    benchmarking = true;
    extbench = 0;
  }

  return UPDATE_CONSOLE_DISPLAY;
}

static auto CmdProfileList(int nArgs) -> UpdateResult;

auto CmdProfile(int nArgs) -> UpdateResult {
  if (nArgs == 0) {
    return CmdProfileList(0);
  }

  int iArg = 1;
  int iParam = 0;
  bool bFound = FindParam(args[iArg].sArg, MATCH_EXACT, iParam,
                          PARAM_PROFILE_BEGIN, PARAM_PROFILE_END) > 0;

  if (bFound) {
    if (iParam == PARAM_PROFILE_RESET) {
      ProfileReset();
    } else if (iParam == PARAM_PROFILE_SAVE) {
      if (ProfileSave()) {
        char sText[CONSOLE_WIDTH];
        ConsoleBufferPushFormat(sText, " Saved: %s", file_name_profile);
      }
    } else if (iParam == PARAM_PROFILE_LIST) {
      return CmdProfileList(0);
    } else {
      profiling = (iParam == PARAM_PROFILE_ON);
      profile_begin_cycles = cumulative_cycles;
    }
  } else {
    return Help_Arg_1(CMD_PROFILE);
  }

  return UPDATE_CONSOLE_DISPLAY;
}

auto ProfileLinePeek(int iLine) -> char* {
  char* text = nullptr;

  iLine = std::max(iLine, 0);

  if (iLine <= profile_line_count) {
    text = &profile_line[iLine][0];
  }

  return text;
}

auto ProfileReset() -> void {
  int opcode = 0;
  for (opcode = 0; opcode < NUM_OPCODES; opcode++) {
    profile_opcodes[opcode].opcode = opcode;
    profile_opcodes[opcode].count = 0;
  }

  int iOpmode = 0;
  for (iOpmode = 0; iOpmode < NUM_OPMODES; iOpmode++) {
    profile_opmodes[iOpmode].opmode = iOpmode;
    profile_opmodes[iOpmode].count = 0;
  }

  profile_line_count = 0;
  profile_begin_cycles = cumulative_cycles;
}

auto ProfileFormat(bool bSeperateColumns, int eFormatMode) -> void {
  (void)bSeperateColumns;
  (void)eFormatMode;
  int opcode = 0;
  int iOpmode = 0;

  bool bOpcodeGood = true;
  bool bOpmodeGood = true;

  std::vector<ProfileOpcode> vProfileOpcode(&profile_opcodes[0],
                                              &profile_opcodes[NUM_OPCODES]);
  std::vector<ProfileOpmode> vProfileOpmode(&profile_opmodes[0],
                                              &profile_opmodes[NUM_OPMODES]);

  // sort >
  std::sort(vProfileOpcode.begin(), vProfileOpcode.end(), ProfileOpcode());
  std::sort(vProfileOpmode.begin(), vProfileOpmode.end(), ProfileOpmode());

  profile_line_count = 0;
  char* text = &profile_line[0][0];

  uint64_t nTotalCycles = cumulative_cycles - profile_begin_cycles;
  snprintf(text, sizeof(profile_line[0]), "Cycles: %llu\n",
           static_cast<unsigned long long>(nTotalCycles));
  profile_line_count++;

  while (bOpcodeGood || bOpmodeGood) {
    text = &profile_line[profile_line_count][0];
    char op_text[CONSOLE_WIDTH] = "";
    char mode_text[CONSOLE_WIDTH] = "";

    if (opcode < NUM_OPCODES) {
      if (vProfileOpcode.at(static_cast<size_t>(opcode)).count > 0) {
        snprintf(op_text, sizeof(op_text), "%s: %llu",
                 opcodes65_c02[vProfileOpcode.at(static_cast<size_t>(opcode))
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
        snprintf(mode_text, sizeof(mode_text), "  %s: %llu",
                 opmodes[static_cast<size_t>(
                               vProfileOpmode.at(static_cast<size_t>(iOpmode))
                                   .opmode)]
                     .name,
                 static_cast<unsigned long long>(
                     vProfileOpmode.at(static_cast<size_t>(iOpmode)).count));
      } else {
        bOpmodeGood = false;
      }
    }

    if (op_text[0] != '\0' || mode_text[0] != '\0') {
      snprintf(text, sizeof(profile_line[0]), "%s%s\n", op_text, mode_text);
      profile_line_count++;
    }

    opcode++;
    iOpmode++;

    if (profile_line_count >= (NUM_PROFILE_LINES - 1)) {
      break;
    }
  }
}

static auto CmdProfileList(int nArgs) -> UpdateResult {
  (void)nArgs;
  ProfileFormat(true, 0);

  int nLines = std::min(profile_line_count, console_display_lines - 1);
  return ConsoleBufferTryUnpause(nLines);
}

auto ProfileSave() -> bool {
  bool bStatus = false;
  FilePtr hFile(fopen(file_name_profile, "w"), fclose);

  if (hFile) {
    ProfileFormat(true, 0);

    char* text = nullptr;
    int nLine = profile_line_count;
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
