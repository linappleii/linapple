// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

constexpr double m14 = (157500000.0 / 11.0);  // 14.3181818... * 10^6
constexpr double clock_6502_ntsc =
    ((m14 * 65.0) / 912.0);                   // 65 cycles per 912 14M clocks
constexpr double clock_6502_pal = 1015625.0;  // PAL 6502 clock (1.015625 MHz)
constexpr double clock_6502 = clock_6502_ntsc;

constexpr int num_slots = 8;
constexpr uint32_t apple2_6502_mem_end = 0xFFFF;

constexpr uint8_t apple2e_mask = 0x10;

enum Apple2Type : uint8_t {
  A2TYPE_APPLE2 = 0,
  A2TYPE_APPLE2PLUS,
  A2TYPE_APPLE2JPLUS,
  A2TYPE_CLONE_PRAVETS82,
  A2TYPE_CLONE_PRAVETS8M,
  A2TYPE_CLONE_BASE64A,
  A2TYPE_APPLE2E = apple2e_mask,
  A2TYPE_APPLE2EENHANCED,
  A2TYPE_CLONE_PRAVETS8C,
  A2TYPE_CLONE_TK3000E,
  A2TYPE_MAX,
};

enum Apple2Language : uint8_t {
  A2LANG_US = 1,
  A2LANG_UK,
  A2LANG_FR,
  A2LANG_DE,
  A2LANG_JP_ROMAN,
  A2LANG_JP_KANA,
};

extern Apple2Type current_apple2_type;
extern Apple2Language current_language;

inline auto is_apple2() noexcept -> bool {
  return (current_apple2_type & apple2e_mask) == 0;
}
