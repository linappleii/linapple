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

#if ENABLE_ROM_CLONE_BASE64A
const uint8_t* const clone_base64a_rom = g_rom_clone_base64a;
const uint8_t* const clone_base64a_german_video_rom =
    g_rom_clone_base64a_german_video;
#endif

#if ENABLE_ROM_CLONE_PRAVETS
const uint8_t* const clone_pravets82_rom = g_rom_clone_pravets82;
const uint8_t* const clone_pravets8c_rom = g_rom_clone_pravets8c;
const uint8_t* const clone_pravets8m_rom = g_rom_clone_pravets8m;
#endif

#if ENABLE_ROM_CLONE_TK3000E
const uint8_t* const clone_tk3000e_rom = g_rom_clone_tk3000e;
#endif

#if ENABLE_ROM_DISK2
const uint8_t* const disk2_rom = g_rom_disk2;
const uint8_t* const disk2_13sector_rom = g_rom_disk2_13sector;
#endif

#if ENABLE_ROM_SSC
const uint8_t* const ssc_rom = g_rom_ssc;
#endif

#if ENABLE_ROM_MOUSE
const uint8_t* const mouse_interface_rom = g_rom_mouse_interface;
#endif

#if ENABLE_ROM_MOCKINGBOARD
const uint8_t* const mockingboard_d_rom = g_rom_mockingboard_d;
#endif

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
