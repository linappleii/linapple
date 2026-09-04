// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <vector>

#include "Debugger_Types.h"
#include "core/Util_Text.h"

const char* ParserFindToken(const char* src_ptr, const TokenTable_t* aTokens,
                            const int nTokens, ArgToken_e* pToken_);
const char* FindTokenOrAlphaNumeric(const char* src_ptr,
                                    const TokenTable_t* aTokens,
                                    const int nTokens, ArgToken_e* pToken_);
int RemoveWhiteSpaceReverse(char* src_ptr);
void TextConvertTabsToSpaces(char* pDeTabified_, const char* text,
                             const int nDstSize, int nTabStop = 0);

inline const char* SkipUntilToken(const char* src_ptr,
                                  const TokenTable_t* aTokens,
                                  const int nTokens, ArgToken_e* pToken_) {
  if (pToken_) *pToken_ = NO_TOKEN;

  while (src_ptr && (*src_ptr)) {
    if (ParserFindToken(src_ptr, aTokens, nTokens, pToken_)) return src_ptr;

    src_ptr++;
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

void util_strupr(char* s);
int FindParam(const char* pLookupName, Match_e eMatch, int& iParam_,
              int iParamBegin = 0, int iParamEnd = NUM_PARAMS - 1);
int FindCommand(const char* pName, CmdFuncPtr_t& pFunction_,
                int* iCommand_ = nullptr);
void DisplayAmbigiousCommands(int nFound);
int ParseInput(char* pConsoleInput, bool bCook = true);

// Arg - Command Processing
Update_t Help_Arg_1(int iCommandHelp);
int Arg_1(int nValue);
int Arg_1(char* pName);
int Arg_Shift(int iSrc, int iEnd, int iDst = 0);
int Args_Insert(int iSrc, int iEnd, int nLen);
void ArgsClear();

bool ArgsGetValue(Arg_t* pArg, uint16_t* pAddressValue_, const int nBase = 16);
bool ArgsGetImmediateValue(Arg_t* pArg, uint16_t* pAddressValue_);
int ArgsGet(char* pInput);
bool ArgsGetRegisterValue(Arg_t* pArg, uint16_t* pAddressValue_);
void ArgsRawParse(void);
int ArgsCook(const int nArgs);
