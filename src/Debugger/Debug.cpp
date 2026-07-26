#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "apple2/SnapshotTypes.h"
#include "Debug.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Memory.h"
#include "Debugger_Cmd_CPU.h"
#include "Debugger_Help.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Assembler.h"
#include "Debugger_Display.h"
#include "Debugger_Symbols.h"
#include "Debugger_Range.h"
#include "Debugger_Color.h"
#include "Debugger_Cmd_Config.h"
#include "Debugger_Cmd_Benchmark.h"
#include "Debugger_Cmd_ZeroPage.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Cmd_Output.h"
#include "Debugger_Commands.h"
#include "Video.h"

// for usleep
#include <unistd.h>
#include <cassert>
#include <cstddef>


enum {
ALLOW_INPUT_LOWERCASE = 1
};

void debug_display(bool bInitDisasm)
{
  if (bInitDisasm) {
    InitDisasm();
  }

  if (DebugVideoMode::Instance().IsSet())
  {
    uint32_t mode = 0;
    DebugVideoMode::Instance().Get(&mode);
    video_refresh_screen(mode, true);
    return;
  }

  UpdateDisplay( UPDATE_ALL );
}

void debug_initialize()
{
    static bool bInitialized = false;
    if (bInitialized) return;

    AssemblerStartup();
    InitDisasm();

    bInitialized = true;
}

auto is_debug_stepping_at_full_speed() -> bool
{
  return (g_state.mode == MODE_STEPPING) && g_bDebugFullSpeed;
}

bool g_debugger_eat_key = false;

uint16_t g_nDisasmTopAddress = 0;
uint16_t g_nDisasmBotAddress = 0;
uint16_t g_nDisasmCurAddress = 0;

bool g_bDisasmCurBad    = false;
int  g_nDisasmCurLine   = 0; // Aligned to Top or Center
int  g_iDisasmCurState = CURSOR_NORMAL;

int  g_nDisasmWinHeight = 0;

int       g_iFontSpacing = FONT_SPACING_CLEAN;
int       g_nFontHeight = 8;
int       g_nDisasmDisplayLines  = 0;

int       g_nWatches = 0;
Watches_t g_aWatches[ MAX_WATCHES ];

int           g_iWindowLast = WINDOW_CODE;
int           g_iWindowThis = WINDOW_CODE;
WindowSplit_t g_aWindowConfig[ NUM_WINDOWS ];

int                g_nZeroPagePointers = 0;
ZeroPagePointers_t g_aZeroPagePointers[ MAX_ZEROPAGE_POINTERS ];

bool GetBreakpointInfo(uint16_t nOffset, bool &bBreakpointActive_, bool &bBreakpointEnable_)
{
  bBreakpointActive_ = false;
  bBreakpointEnable_ = false;
  for (int i = 0; i < g_nBreakpoints; i++)
  {
    if (g_aBreakpoints[i].bSet && g_aBreakpoints[i].nAddress == nOffset)
    {
      bBreakpointActive_ = true;
      bBreakpointEnable_ = g_aBreakpoints[i].bEnabled;
      return true;
    }
  }
  return false;
}

const int DEBUGGER_VERSION = MAKE_VERSION(2,9,0,15);

const int WINDOW_DATA_BYTES_PER_LINE = 8;

DebugVideoMode DebugVideoMode::m_Instance;
