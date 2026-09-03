// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

constexpr double M14 = (157500000.0 / 11.0);  // 14.3181818... * 10^6
constexpr double CLOCK_6502 =
    ((M14 * 65.0) / 912.0);                   // 65 cycles per 912 14M clocks
constexpr double CLK_Z80 = (CLOCK_6502 * 2);  // Z-80 clock rate is 2.041MHz

constexpr uint32_t uCyclesPerLine = 65;  // 25 cycles HBL & 40 cycles HBL
constexpr uint32_t uVisibleLinesPerFrame = 64 * 3;  // 192 visible lines
constexpr uint32_t uLinesPerFrame = 262;            // 192 visible + 70 VBL

constexpr int NUM_SLOTS = 8;
constexpr uint32_t _6502_MEM_END = 0xFFFF;
constexpr uint32_t _6502_MEM_LEN = _6502_MEM_END + 1;

constexpr uint8_t APPLE2E_MASK = 0x10;
constexpr uint8_t APPLE2C_MASK = 0x20;

enum eApple2Type {
  A2TYPE_APPLE2 = 0,
  A2TYPE_APPLE2PLUS,
  A2TYPE_APPLE2JPLUS,
  A2TYPE_APPLE2E = APPLE2E_MASK,
  A2TYPE_APPLE2EENHANCED,
  A2TYPE_MAX
};

enum eApple2Language {
  A2LANG_US = 1,
  A2LANG_UK,
  A2LANG_FR,
  A2LANG_DE,
  A2LANG_JP_ROMAN,
  A2LANG_JP_KANA
};

extern eApple2Type g_apple2_type;
extern eApple2Language g_language;

inline auto IS_APPLE2() -> bool {
  return (g_apple2_type & (APPLE2E_MASK | APPLE2C_MASK)) == 0;
}
inline auto IS_APPLE2E() -> bool { return (g_apple2_type & APPLE2E_MASK) != 0; }
inline auto IS_APPLE2C() -> bool { return (g_apple2_type & APPLE2C_MASK) != 0; }
