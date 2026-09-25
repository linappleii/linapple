// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

constexpr double M14 = (157500000.0 / 11.0);  // 14.3181818... * 10^6
constexpr double CLOCK_6502_NTSC =
    ((M14 * 65.0) / 912.0);                   // 65 cycles per 912 14M clocks
constexpr double CLOCK_6502_PAL = 1015625.0;  // PAL 6502 clock (1.015625 MHz)
constexpr double CLOCK_6502 = CLOCK_6502_NTSC;

constexpr int NUM_SLOTS = 8;
constexpr uint32_t APPLE2_6502_MEM_END = 0xFFFF;

constexpr uint8_t APPLE2E_MASK = 0x10;

enum Apple2Type_t {
  A2TYPE_APPLE2 = 0,
  A2TYPE_APPLE2PLUS,
  A2TYPE_APPLE2JPLUS,
  A2TYPE_CLONE_PRAVETS82,
  A2TYPE_CLONE_PRAVETS8M,
  A2TYPE_CLONE_BASE64A,
  A2TYPE_APPLE2E = APPLE2E_MASK,
  A2TYPE_APPLE2EENHANCED,
  A2TYPE_CLONE_PRAVETS8C,
  A2TYPE_CLONE_TK3000E,
  A2TYPE_MAX
};
using eApple2Type = Apple2Type_t;

enum Apple2Language_t {
  A2LANG_US = 1,
  A2LANG_UK,
  A2LANG_FR,
  A2LANG_DE,
  A2LANG_JP_ROMAN,
  A2LANG_JP_KANA
};
using eApple2Language = Apple2Language_t;

extern Apple2Type_t g_apple2_type;
extern Apple2Language_t g_language;

inline auto is_apple2() -> bool { return (g_apple2_type & APPLE2E_MASK) == 0; }
inline auto IS_APPLE2() -> bool { return is_apple2(); }
