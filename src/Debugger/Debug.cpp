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
  return (g_state.mode == MODE_STEPPING) && g_debug_full_speed;
}

bool g_debugger_eat_key = false;

uint16_t g_disasm_top_address = 0;
uint16_t g_disasm_bot_address = 0;
uint16_t g_disasm_cur_address = 0;

bool g_disasm_cur_bad    = false;
int  g_disasm_cur_line   = 0; // Aligned to Top or Center
int  g_disasm_cur_state = CURSOR_NORMAL;

int  g_disasm_win_height = 0;

int       g_font_spacing = FONT_SPACING_CLEAN;
int       g_font_height = 8;
int       g_disasm_display_lines  = 0;

int       g_watches_count = 0;
Watches_t g_watches[ MAX_WATCHES ];

int           g_window_last = WINDOW_CODE;
int           g_window_this = WINDOW_CODE;
WindowSplit_t g_window_config[ NUM_WINDOWS ];

int                g_zero_page_pointers_count = 0;
ZeroPagePointers_t g_zero_page_pointers[ MAX_ZEROPAGE_POINTERS ];

bool GetBreakpointInfo(uint16_t nOffset, bool &bBreakpointActive_, bool &bBreakpointEnable_)
{
  bBreakpointActive_ = false;
  bBreakpointEnable_ = false;
  for (int i = 0; i < g_breakpoints_count; i++)
  {
    if (g_breakpoints[i].bSet && g_breakpoints[i].nAddress == nOffset)
    {
      bBreakpointActive_ = true;
      bBreakpointEnable_ = g_breakpoints[i].bEnabled;
      return true;
    }
  }
  return false;
}

const int DEBUGGER_VERSION = MAKE_VERSION(2,9,0,15);

const int WINDOW_DATA_BYTES_PER_LINE = 8;

DebugVideoMode DebugVideoMode::m_Instance;
