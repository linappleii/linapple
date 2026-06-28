// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

constexpr std::size_t cx_rom_size = 0x0800;       // 2 KB ($C000-$CFFF)
constexpr std::size_t apple2_rom_size = 0x3000;   // 12 KB ($D000-$FFFF)
constexpr std::size_t apple2e_rom_size = 0x3800;  // 14 KB ($C000-$FFFF)

extern const char apple2_rom[];
extern const char apple2_plus_rom[];
extern const char apple2e_rom[];
extern const char apple2e_enhanced_rom[];

#ifdef __cplusplus
}
#endif
