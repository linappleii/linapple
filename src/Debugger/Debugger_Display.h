// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#include "Debugger_Color.h"
#include "Debugger_Console.h"

struct Rect;

enum ConsoleFontSize : uint8_t {
  CONSOLE_FONT_GRID_X = 8,
  CONSOLE_FONT_GRID_Y = 8,

  CONSOLE_FONT_WIDTH = 7,
  CONSOLE_FONT_HEIGHT = 8,
};

extern ColorRef console_brush_fg;
extern ColorRef console_brush_bg;
struct VideoSurface;
extern VideoSurface* debug_screen;
extern int display_memory_lines;

enum : uint16_t {
  DISPLAY_WIDTH = 560,
  DISPLAY_HEIGHT = 384,
  DISPLAY_DISASM_RIGHT = 353,
  MAX_DISPLAY_LINES = DISPLAY_HEIGHT / CONSOLE_FONT_HEIGHT,
};

auto GetConsoleTopPixels(int y) -> int;

extern FontConfig font_config[NUM_FONTS];

auto DebuggerSetColorFG(ColorRef nRGB) -> void;
auto DebuggerSetColorBG(ColorRef nRGB, bool bTransparent = false) -> void;

auto PrintGlyph(int x, int y, int glyph) -> void;
auto PrintText(const char* text, Rect& rRect) -> int;
auto PrintTextCursorX(const char* text, Rect& rRect) -> int;
auto PrintTextCursorY(const char* text, Rect& rRect) -> int;

auto PrintTextColor(const ConChar* text, Rect& rRect) -> void;

auto GetDebugViewPortScale(float* x, float* y) -> void;

auto DrawWindow_Source(UpdateResult bUpdate) -> void;

auto DrawBreakpoints(int line) -> void;
auto DrawConsoleInput() -> void;
auto DrawConsoleLine(const ConChar* text, int y) -> void;
auto DrawConsoleCursor() -> void;

auto GetDisassemblyLine(uint16_t nBaseAddress, DisasmLine& line_) -> int;
auto DrawDisassemblyLine(int iLine, uint16_t nBaseAddress) -> uint16_t;
auto FormatDisassemblyLine(const DisasmLine& line, char* sDisassembly_,
                           int nBufferSize) -> void;
auto FormatOpcodeBytes(uint16_t nBaseAddress, DisasmLine& line_) -> void;
auto FormatNopcodeBytes(uint16_t nBaseAddress, DisasmLine& line_) -> void;

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
auto UpdateDisplay(UpdateResult bUpdate) -> void;

enum DebugVirtualTextScreen : uint8_t {
  DEBUG_VIRTUAL_TEXT_WIDTH = 80,
  DEBUG_VIRTUAL_TEXT_HEIGHT = 48,
};

extern char debugger_virtual_text_screen[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                          [DEBUG_VIRTUAL_TEXT_WIDTH];
extern ColorRef debugger_virtual_text_screen_fg[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                                   [DEBUG_VIRTUAL_TEXT_WIDTH];
extern ColorRef debugger_virtual_text_screen_bg[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                                   [DEBUG_VIRTUAL_TEXT_WIDTH];
extern auto Util_GetDebuggerText(char*& pText_)
    -> size_t;  // Same API as Util_GetTextScreen()

auto DrawWindow_Code(UpdateResult bUpdate) -> void;
auto DrawWindow_Console(UpdateResult bUpdate) -> void;
auto DrawWindow_Data(UpdateResult bUpdate) -> void;
auto DrawWindow_IO(UpdateResult bUpdate) -> void;
auto DrawWindow_Symbols(UpdateResult bUpdate) -> void;
auto DrawWindow_ZeroPage(UpdateResult bUpdate) -> void;

auto DrawSourceLine(int iSourceLine, Rect& rect) -> void;

auto ColorizeSpecialChar(char* sText, uint8_t nData, MemoryView iView,
                         int iAsciBackground = BG_INFO,
                         int iTextForeground = FG_DISASM_CHAR,
                         int iHighBackground = BG_INFO_CHAR,
                         int iHighForeground = FG_INFO_CHAR_HI,
                         int iCtrlBackground = BG_INFO_CHAR,
                         int iCtrlForeground = FG_INFO_CHAR_LO) -> char;

auto SetupColorsHiLoBits(bool bHighBit, bool bCtrlBit, int iTextBG, int iTextFG,
                         int iHighBG, int iHighFG, int iCtrlBG, int iCtrlFG)
    -> void;

auto ColorizeFlags(bool bSet, int bg_default = BG_INFO,
                   int fg_default = FG_INFO_REG) -> void;

auto DrawWindowBottom(UpdateResult bUpdate, int iWindow) -> void;
auto DrawSubWindow_Info(UpdateResult bUpdate, int iWindow) -> void;
auto DrawSubWindow_Code(int iWindow) -> void;
auto DrawSubWindow_Source(UpdateResult bUpdate) -> void;
auto DrawSubWindow_Source2(UpdateResult bUpdate) -> void;
auto DrawSubWindow_IO(UpdateResult bUpdate) -> void;
auto FillRect(const Rect* r, int Brush) -> void;
auto DrawSubWindow_Symbols(UpdateResult bUpdate) -> void;
auto DrawSubWindow_ZeroPage(UpdateResult bUpdate) -> void;
auto DrawSubWindow_Console(UpdateResult bUpdate) -> void;

auto DrawWindowBackground_Main(int iWindow) -> void;
auto DrawWindowBackground_Info(int iWindow) -> void;
auto DrawRegister(int line, const char* name, int nBytes, uint16_t nValue,
                  int iSource) -> void;
auto GetTargets_IgnoreDirectJSRJMP(uint8_t opcode, int& nTargetPointer) -> void;

extern VideoScannerDisplayInfo video_scanner_display_info;
