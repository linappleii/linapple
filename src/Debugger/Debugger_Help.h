// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "Debugger_Types.h"

// Types ____________________________________________________________________

enum HelpType_e {
  HELP_TYPE_USAGE,
  HELP_TYPE_NOTE,
  HELP_TYPE_EXAMPLE,
  HELP_TYPE_RANGE,
  HELP_TYPE_SEE_ALSO,
  NUM_HELP_TYPES
};

struct HelpEntry_t {
  int iCommand;
  HelpType_e eType;
  const char* text;
};

// Prototypes _______________________________________________________________

auto HelpLastCommand() -> Update_t;

constexpr uint32_t BYTE3_SHIFT = 24;
constexpr uint32_t BYTE2_SHIFT = 16;
constexpr uint32_t BYTE1_SHIFT = 8;
constexpr uint32_t BYTE_MASK = 0xFF;

inline auto UnpackVersion(const uint32_t nVersion, int& nMajor_, int& nMinor_,
                          int& nFixMajor_, int& nFixMinor_) -> void {
  nMajor_ = static_cast<int>((nVersion >> BYTE3_SHIFT) & BYTE_MASK);
  nMinor_ = static_cast<int>((nVersion >> BYTE2_SHIFT) & BYTE_MASK);
  nFixMajor_ = static_cast<int>((nVersion >> BYTE1_SHIFT) & BYTE_MASK);
  nFixMinor_ = static_cast<int>(nVersion & BYTE_MASK);
}

auto TestStringCat(char* pDst, const char* src_ptr, const int nDstSize) -> bool;
auto TryStringCat(char* pDst, const char* src_ptr, const int nDstSize) -> bool;
auto StringCat(char* pDst, const char* src_ptr, const int nDstSize) -> int;
