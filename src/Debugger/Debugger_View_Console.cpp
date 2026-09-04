// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "Debug.h"
#include "Debugger_Color.h"
#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Types.h"
#include "apple2/Video.h"

// Externs for globals in Debugger_Display.cpp
extern int g_console_display_start;
extern int g_console_display_lines;
extern int g_console_display_total;
extern int g_console_display_width;
extern conchar_t g_console_display[CONSOLE_HEIGHT][CONSOLE_WIDTH];
extern uint32_t g_console_brush_bg;

// Functions moved from Debugger_Display.cpp

auto DrawSubWindow_Console(Update_t bUpdate) -> void {
  if (!can_draw_debugger()) {
    return;
  }

#if !USE_APPLE_FONT
  SelectObject(GetDebuggerMemDC(), g_font_config[FONT_CONSOLE].h_font);
#endif

  if ((bUpdate & UPDATE_CONSOLE_DISPLAY) || (bUpdate & UPDATE_CONSOLE_INPUT)) {
    DebuggerSetColorBG(DebuggerGetColor(BG_CONSOLE_OUTPUT));

    int iLine = g_console_display_start + CONSOLE_FIRST_LINE;
    for (int y = 1; y < g_console_display_lines; y++) {
      if (iLine <= (g_console_display_total + CONSOLE_FIRST_LINE)) {
        DebuggerSetColorFG(DebuggerGetColor(FG_CONSOLE_OUTPUT));
        DrawConsoleLine(g_console_display[iLine], y);
      } else {
        DrawConsoleLine(nullptr, y);
      }
      iLine++;
    }

    DrawConsoleInput();
  }
}

auto DrawWindow_Console(Update_t bUpdate) -> void { (void)bUpdate; }

auto DrawWindowBackground_Main(int iWindow) -> void {
  (void)iWindow;
  DebuggerSetColorBG(DebuggerGetColor(BG_DISASM_1));

#if !DEBUG_FONT_NO_BACKGROUND_FILL_MAIN
  Rect_t rect;
  rect.left = 0;
  rect.top = 0;
  rect.right = DISPLAY_DISASM_RIGHT;
  int nTop = GetConsoleTopPixels(g_console_display_lines - 1);
  rect.bottom = nTop;
  FillRect(&rect, g_console_brush_bg);
#endif
}

auto DrawWindowBackground_Info(int iWindow) -> void {
  (void)iWindow;
  DebuggerSetColorBG(DebuggerGetColor(BG_INFO));

#if !DEBUG_FONT_NO_BACKGROUND_FILL_INFO
  Rect_t rect;
  rect.top = 0;
  rect.left = DISPLAY_DISASM_RIGHT;
  rect.right = DISPLAY_WIDTH;
  int nTop = GetConsoleTopPixels(g_console_display_lines - 1);
  rect.bottom = nTop;
  FillRect(&rect, g_console_brush_bg);
#endif
}
