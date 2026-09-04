// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "Debugger_Color.h"
#include "Debugger_Console.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

struct Rect_t;

#define USE_APPLE_FONT 1

#define DEBUG_APPLE_FONT 0

#define APPLE_FONT_NEW 1

#if APPLE_FONT_NEW
#define APPLE_FONT_BITMAP_PADDED 0
#else
#define APPLE_FONT_BITMAP_PADDED 1
#endif

enum ConsoleFontSize_e {
  CONSOLE_FONT_GRID_X = 8,
  CONSOLE_FONT_GRID_Y = 8,

  CONSOLE_FONT_WIDTH = 7,
  CONSOLE_FONT_HEIGHT = 8,
};

extern ColorRef_t g_console_brush_fg;
extern ColorRef_t g_console_brush_bg;

enum {
  DISPLAY_WIDTH = 560,
  DISPLAY_HEIGHT = 384,
  DISPLAY_DISASM_RIGHT = 353,
  MAX_DISPLAY_LINES = DISPLAY_HEIGHT / CONSOLE_FONT_HEIGHT,
};

auto GetConsoleTopPixels(int y) -> int;

extern FontConfig_t g_font_config[NUM_FONTS];

auto DebuggerSetColorFG(ColorRef_t nRGB) -> void;
auto DebuggerSetColorBG(ColorRef_t nRGB, bool bTransparent = false) -> void;

auto PrintGlyph(const int x, const int y, const int iChar) -> void;
auto PrintText(const char* text, Rect_t& rRect) -> int;
auto PrintTextCursorX(const char* text, Rect_t& rRect) -> int;
auto PrintTextCursorY(const char* text, Rect_t& rRect) -> int;

auto PrintTextColor(const conchar_t* text, Rect_t& rRect) -> void;

auto GetDebugViewPortScale(float* x, float* y) -> void;

auto DrawWindow_Source(Update_t bUpdate) -> void;

auto DrawBreakpoints(int line) -> void;
auto DrawConsoleInput() -> void;
auto DrawConsoleLine(const conchar_t* text, int y) -> void;
auto DrawConsoleCursor() -> void;

auto GetDisassemblyLine(const uint16_t nOffset, DisasmLine_t& line_) -> int;
auto DrawDisassemblyLine(int line, const uint16_t offset) -> uint16_t;
auto FormatDisassemblyLine(const DisasmLine_t& line, char* sDisassembly_,
                           const int nBufferSize) -> void;
auto FormatOpcodeBytes(uint16_t nBaseAddress, DisasmLine_t& line_) -> void;
auto FormatNopcodeBytes(uint16_t nBaseAddress, DisasmLine_t& line_) -> void;

auto DrawFlags(int line, uint16_t nRegFlags, char* pFlagNames_) -> void;
auto DrawStack(int line) -> void;
auto DrawMemory(int line, int iMemDump) -> void;
auto DrawRegisters(int line) -> void;
auto DrawSoftSwitches(int iSoftSwitch) -> void;
auto DrawTargets(int line) -> void;
auto DrawWatches(int line) -> void;
auto DrawZeroPagePointers(int line) -> void;
auto DrawVideoScannerInfo(int line) -> void;

extern auto AllocateDebuggerMemDC(void) -> void;
extern auto ReleaseDebuggerMemDC(void) -> void;
extern auto stretch_blt_mem_to_frame_dc(void) -> void;
auto can_draw_debugger(void) -> bool;

auto InitDisasm(void) -> void;
auto UpdateDisplay(Update_t bUpdate) -> void;

enum DebugVirtualTextScreen_e {
  DEBUG_VIRTUAL_TEXT_WIDTH = 80,
  DEBUG_VIRTUAL_TEXT_HEIGHT = 48
};

extern char g_debugger_virtual_text_screen[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                          [DEBUG_VIRTUAL_TEXT_WIDTH];
extern ColorRef_t g_debugger_virtual_text_screen_fg[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                                   [DEBUG_VIRTUAL_TEXT_WIDTH];
extern ColorRef_t g_debugger_virtual_text_screen_bg[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                                   [DEBUG_VIRTUAL_TEXT_WIDTH];
extern auto Util_GetDebuggerText(char*& pText_)
    -> size_t;  // Same API as Util_GetTextScreen()

extern uint64_t g_cumulative_cycles;

auto DrawWindow_Code(Update_t bUpdate) -> void;
auto DrawWindow_Console(Update_t bUpdate) -> void;
auto DrawWindow_Data(Update_t bUpdate) -> void;
auto DrawWindow_IO(Update_t bUpdate) -> void;
auto DrawWindow_Symbols(Update_t bUpdate) -> void;
auto DrawWindow_ZeroPage(Update_t bUpdate) -> void;

auto DrawSourceLine(int iSourceLine, Rect_t& rect) -> void;

char ColorizeSpecialChar(char* sText, uint8_t nData, const MemoryView_e iView,
                         const int iAsciBackground = BG_INFO,
                         const int iTextForeground = FG_DISASM_CHAR,
                         const int iHighBackground = BG_INFO_CHAR,
                         const int iHighForeground = FG_INFO_CHAR_HI,
                         const int iCtrlBackground = BG_INFO_CHAR,
                         const int iCtrlForeground = FG_INFO_CHAR_LO);

auto SetupColorsHiLoBits(bool bHighBit, bool bCtrlBit, int iTextBG, int iTextFG,
                         int iHighBG, int iHighFG, int iCtrlBG, int iCtrlFG)
    -> void;

auto DrawWindowBottom(Update_t bUpdate, int iWindow) -> void;
auto DrawSubWindow_Info(Update_t bUpdate, int iWindow) -> void;
auto DrawSubWindow_Code(int iWindow) -> void;
auto DrawSubWindow_Source(Update_t bUpdate) -> void;
auto DrawSubWindow_Source2(Update_t bUpdate) -> void;
auto DrawSubWindow_IO(Update_t bUpdate) -> void;
auto FillRect(const Rect_t* r, int Brush) -> void;
auto DrawSubWindow_Symbols(Update_t bUpdate) -> void;
auto DrawSubWindow_ZeroPage(Update_t bUpdate) -> void;
auto DrawSubWindow_Console(Update_t bUpdate) -> void;
auto DrawWindow_Data(Update_t bUpdate) -> void;
auto DrawWindow_IO(Update_t bUpdate) -> void;
auto DrawWindow_Symbols(Update_t bUpdate) -> void;
auto DrawWindow_ZeroPage(Update_t bUpdate) -> void;
auto DrawWindow_Console(Update_t bUpdate) -> void;
auto DrawWindowBackground_Main(int iWindow) -> void;
auto DrawWindowBackground_Info(int iWindow) -> void;
auto DrawRegister(int line, const char* name, const int nBytes,
                  const uint16_t nValue, int iSource) -> void;
auto GetTargets_IgnoreDirectJSRJMP(const uint8_t opcode, int& nTargetPointer)
    -> void;

extern VideoScannerDisplayInfo_t g_video_scanner_display_info;
