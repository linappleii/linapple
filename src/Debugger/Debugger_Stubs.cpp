// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "frontends/common/VideoSurface.h"

// NOLINTBEGIN(cppcoreguidelines-use-enum-class,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,modernize-use-trailing-return-type)
// Dummy coordinates and configurations for linkers
using ColorRef_t = uint32_t;
enum DebugVirtualTextScreen_e {
  DEBUG_VIRTUAL_TEXT_WIDTH = 80,
  DEBUG_VIRTUAL_TEXT_HEIGHT = 48
};

VideoSurface* g_hDebugScreen = nullptr;
bool g_bDebuggerEatKey = false;

char g_aDebuggerVirtualTextScreen[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                 [DEBUG_VIRTUAL_TEXT_WIDTH] = {};
ColorRef_t g_aDebuggerVirtualTextScreenFG[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                         [DEBUG_VIRTUAL_TEXT_WIDTH] = {};
ColorRef_t g_aDebuggerVirtualTextScreenBG[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                         [DEBUG_VIRTUAL_TEXT_WIDTH] = {};

void DebugBegin() {}
void DebugEnd() {}
void DebugDestroy() {}
void DebugInitialize() {}
void DebugDisplay(bool) {}
void DebuggerProcessKey(int) {}
void DebuggerInputConsoleChar(char) {}
void DebuggerMouseClick(int, int) {}
bool DebugGetVideoMode(uint32_t*) { return false; }
// NOLINTEND(cppcoreguidelines-use-enum-class,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,modernize-use-trailing-return-type)
