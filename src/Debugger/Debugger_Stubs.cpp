// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "frontends/common/VideoSurface.h"

// NOLINTBEGIN(cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, modernize-use-trailing-return-type)
// Dummy coordinates and configurations for
// linkers
using ColorRef_t = uint32_t;
enum DebugVirtualTextScreen_e {
  DEBUG_VIRTUAL_TEXT_WIDTH = 80,
  DEBUG_VIRTUAL_TEXT_HEIGHT = 48
};

VideoSurface_t* g_debug_screen = nullptr;
bool g_debugger_eat_key = false;

char g_debugger_virtual_text_screen[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                   [DEBUG_VIRTUAL_TEXT_WIDTH] = {};
ColorRef_t g_debugger_virtual_text_screen_fg[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                            [DEBUG_VIRTUAL_TEXT_WIDTH] = {};
ColorRef_t g_debugger_virtual_text_screen_bg[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                            [DEBUG_VIRTUAL_TEXT_WIDTH] = {};

auto debug_begin() -> void {}
auto debug_end() -> void {}
auto debug_destroy() -> void {}
auto debug_initialize() -> void {}
auto debug_display(bool) -> void {}
auto debugger_process_key(int) -> void {}
auto debugger_input_console_char(char) -> void {}
auto debugger_mouse_click(int, int) -> void {}
auto debug_get_video_mode(uint32_t*) -> bool { return false; }
// NOLINTEND(cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, modernize-use-trailing-return-type)
