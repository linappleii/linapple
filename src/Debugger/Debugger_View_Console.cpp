#include "core/Common.h"
#include "Debugger_Display.h"
#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Color.h"

// Externs for globals in Debugger_Display.cpp
extern int g_iConsoleDisplayStart;
extern int g_nConsoleDisplayLines;
extern int g_nConsoleDisplayTotal;
extern int g_nConsoleDisplayWidth;
extern conchar_t g_aConsoleDisplay[CONSOLE_HEIGHT][CONSOLE_WIDTH];
extern int g_hConsoleBrushBG;

// Constants
const int DISPLAY_DISASM_RIGHT = 353;
const int DISPLAY_WIDTH = 560;

// Functions moved from Debugger_Display.cpp

void DrawSubWindow_Console(Update_t bUpdate)
{
  if (!CanDrawDebugger())
    return;

#if !USE_APPLE_FONT
  SelectObject(GetDebuggerMemDC(), g_aFontConfig[FONT_CONSOLE]._hFont);
#endif

  if ((bUpdate & UPDATE_CONSOLE_DISPLAY) || (bUpdate & UPDATE_CONSOLE_INPUT))
  {
    DebuggerSetColorBG(DebuggerGetColor(BG_CONSOLE_OUTPUT));

    int iLine = g_iConsoleDisplayStart + CONSOLE_FIRST_LINE;
    for (int y = 1; y < g_nConsoleDisplayLines; y++)
    {
      if (iLine <= (g_nConsoleDisplayTotal + CONSOLE_FIRST_LINE))
      {
        DebuggerSetColorFG(DebuggerGetColor(FG_CONSOLE_OUTPUT));
        DrawConsoleLine(g_aConsoleDisplay[iLine], y);
      }
      else
      {
        DrawConsoleLine(NULL, y);
      }
      iLine++;
    }

    DrawConsoleInput();
  }
}

void DrawWindow_Console(Update_t bUpdate)
{
  (void)bUpdate;
}

void DrawWindowBackground_Main(int iWindow)
{
  (void)iWindow;
  DebuggerSetColorBG(DebuggerGetColor(BG_DISASM_1));

#if !DEBUG_FONT_NO_BACKGROUND_FILL_MAIN
  RECT rect;
  rect.left = 0;
  rect.top = 0;
  rect.right = DISPLAY_DISASM_RIGHT;
  int nTop = GetConsoleTopPixels(g_nConsoleDisplayLines - 1);
  rect.bottom = nTop;
  FillRect(&rect, g_hConsoleBrushBG);
#endif
}

void DrawWindowBackground_Info(int iWindow)
{
  (void)iWindow;
  DebuggerSetColorBG(DebuggerGetColor(BG_INFO));

#if !DEBUG_FONT_NO_BACKGROUND_FILL_INFO
  RECT rect;
  rect.top = 0;
  rect.left = DISPLAY_DISASM_RIGHT;
  rect.right = DISPLAY_WIDTH;
  int nTop = GetConsoleTopPixels(g_nConsoleDisplayLines - 1);
  rect.bottom = nTop;
  FillRect(&rect, g_hConsoleBrushBG);
#endif
}
