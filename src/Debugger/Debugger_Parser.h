// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <vector>

#include "Debugger_Types.h"

auto ParserFindToken(const char* src_ptr, const TokenTable_t* aTokens,
                     const int nTokens, ArgToken_e* pToken_) -> const char*;
auto FindTokenOrAlphaNumeric(const char* src_ptr, const TokenTable_t* aTokens,
                             const int nTokens, ArgToken_e* pToken_) -> const
    char*;
auto RemoveWhiteSpaceReverse(char* src_ptr) -> int;
auto TextConvertTabsToSpaces(char* pDeTabified_, const char* text,
                             const int nDstSize, int nTabStop = 0) -> void;

inline auto SkipUntilToken(const char* src_ptr, const TokenTable_t* aTokens,
                           const int nTokens, ArgToken_e* pToken_) -> const
    char* {
  if (pToken_) *pToken_ = NO_TOKEN;

  while (src_ptr && (*src_ptr)) {
    if (ParserFindToken(src_ptr, aTokens, nTokens, pToken_)) return src_ptr;

    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_white_space(char* src_ptr) -> char* {
  while (src_ptr != nullptr && ((*src_ptr == ' ') || (*src_ptr == '\t'))) {
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_white_space(const char* src_ptr) -> const char* {
  while (src_ptr != nullptr && ((*src_ptr == ' ') || (*src_ptr == '\t'))) {
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_white_space_reverse(char* src_ptr, const char* start)
    -> char* {
  while (src_ptr != nullptr && ((*src_ptr == ' ') || (*src_ptr == '\t')) &&
         (src_ptr > start)) {
    src_ptr--;
  }
  return src_ptr;
}

inline auto skip_white_space_reverse(const char* src_ptr, const char* start)
    -> const char* {
  while (src_ptr != nullptr && ((*src_ptr == ' ') || (*src_ptr == '\t')) &&
         (src_ptr > start)) {
    src_ptr--;
  }
  return src_ptr;
}

inline auto skip_until_char(char* src_ptr, char delim) -> char* {
  while (src_ptr != nullptr && (*src_ptr != '\0')) {
    if (*src_ptr == delim) {
      break;
    }
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_until_char(const char* src_ptr, char delim) -> const char* {
  while (src_ptr != nullptr && (*src_ptr != '\0')) {
    if (*src_ptr == delim) {
      break;
    }
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_until_eol(char* src_ptr) -> char* {
  while (src_ptr != nullptr && (*src_ptr != '\0')) {
    if ((*src_ptr == '\n') || (*src_ptr == '\r')) {
      break;
    }
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_until_eol(const char* src_ptr) -> const char* {
  while (src_ptr != nullptr && (*src_ptr != '\0')) {
    if ((*src_ptr == '\n') || (*src_ptr == '\r')) {
      break;
    }
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_until_white_space(char* src_ptr) -> char* {
  while (src_ptr != nullptr && (*src_ptr != '\0')) {
    if ((*src_ptr == ' ') || (*src_ptr == '\t')) {
      break;
    }
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_until_white_space(const char* src_ptr) -> const char* {
  while (src_ptr != nullptr && (*src_ptr != '\0')) {
    if ((*src_ptr == ' ') || (*src_ptr == '\t')) {
      break;
    }
    src_ptr++;
  }
  return src_ptr;
}

inline auto skip_until_white_space_reverse(char* src_ptr, const char* start)
    -> char* {
  while (src_ptr != nullptr && (src_ptr > start)) {
    if ((*src_ptr == ' ') || (*src_ptr == '\t')) {
      break;
    }
    src_ptr--;
  }
  return src_ptr;
}

inline auto skip_until_white_space_reverse(const char* src_ptr,
                                           const char* start) -> const char* {
  while (src_ptr != nullptr && (src_ptr > start)) {
    if ((*src_ptr == ' ') || (*src_ptr == '\t')) {
      break;
    }
    src_ptr--;
  }
  return src_ptr;
}

// Globals __________________________________________________________________

extern int g_arg_raw_count;
extern Arg_t g_arg_raw[MAX_ARGS];  // pre-processing
extern Arg_t g_args[MAX_ARGS];     // post-processing

extern const char* g_console_first_arg;  // points to first arg

extern const TokenTable_t g_tokens[NUM_TOKENS];

extern std::vector<int> g_potential_commands;

// Prototypes _______________________________________________________________

auto util_strupr(char* s) -> void;
auto FindParam(const char* pLookupName, Match_e eMatch, int& iParam_,
               int iParamBegin = 0, int iParamEnd = NUM_PARAMS - 1) -> int;
auto FindCommand(const char* pName, CmdFuncPtr_t& pFunction_,
                 int* iCommand_ = nullptr) -> int;
auto DisplayAmbigiousCommands(int nFound) -> void;
auto ParseInput(char* pConsoleInput, bool bCook = true) -> int;

// Arg - Command Processing
auto Help_Arg_1(int iCommandHelp) -> Update_t;
auto Arg_1(int nValue) -> int;
auto Arg_1(char* pName) -> int;
auto Arg_Shift(int iSrc, int iEnd, int iDst = 0) -> int;
auto Args_Insert(int iSrc, int iEnd, int nLen) -> int;
auto ArgsClear() -> void;

auto ArgsGetValue(Arg_t* pArg, uint16_t* pAddressValue_, const int nBase = 16)
    -> bool;
auto ArgsGetImmediateValue(Arg_t* pArg, uint16_t* pAddressValue_) -> bool;
auto ArgsGet(char* pInput) -> int;
auto ArgsGetRegisterValue(Arg_t* pArg, uint16_t* pAddressValue_) -> bool;
auto ArgsRawParse(void) -> void;
auto ArgsCook(const int nArgs) -> int;
