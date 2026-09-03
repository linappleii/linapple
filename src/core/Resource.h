// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#include "EmbeddedRoms.h"

#ifdef __cplusplus
extern "C" {
#endif

constexpr std::size_t cx_rom_size = 0x1000;       // 4 KB ($C000-$CFFF)
constexpr std::size_t apple2_rom_size = 0x3000;   // 12 KB ($D000-$FFFF)
constexpr std::size_t apple2e_rom_size = 0x4000;  // 16 KB ($C000-$FFFF)

#if ENABLE_ROM_APPLE2
extern const uint8_t* const apple2_rom;
#endif

#if ENABLE_ROM_APPLE2PLUS
extern const uint8_t* const apple2_plus_rom;
#endif

#if ENABLE_ROM_APPLE2_JPLUS
extern const uint8_t* const apple2_jplus_rom;
extern const uint8_t* const apple2_jplus_video_rom;
#endif

#if ENABLE_ROM_APPLE2E
extern const uint8_t* const apple2e_rom;
#endif

#if ENABLE_ROM_APPLE2ENHANCED
extern const uint8_t* const apple2e_enhanced_rom;
extern const uint8_t* const apple2e_enhanced_video_rom;
#endif

#ifdef __cplusplus
}
#endif
