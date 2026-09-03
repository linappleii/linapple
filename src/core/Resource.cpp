// SPDX-License-Identifier: GPL-2.0-only
#include "core/Resource.h"

#include <cstdint>

#include "EmbeddedRoms.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)

#if ENABLE_ROM_APPLE2
const uint8_t* const apple2_rom = g_rom_apple2;
#endif

#if ENABLE_ROM_APPLE2PLUS
const uint8_t* const apple2_plus_rom = g_rom_apple2_plus;
#endif

#if ENABLE_ROM_APPLE2_JPLUS
const uint8_t* const apple2_jplus_rom = g_rom_apple2_jplus;
const uint8_t* const apple2_jplus_video_rom = g_rom_apple2_jplus_video;
#endif

#if ENABLE_ROM_APPLE2E
const uint8_t* const apple2e_rom = g_rom_apple2e;
#endif

#if ENABLE_ROM_APPLE2ENHANCED
const uint8_t* const apple2e_enhanced_rom = g_rom_apple2e_enhanced;
const uint8_t* const apple2e_enhanced_video_rom = g_rom_apple2e_enhanced_video;
#endif

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
