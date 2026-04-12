#include "core/Common.h"
#include "apple2/Structs.h"
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


#define ALLOW_INPUT_LOWERCASE 1

void DebugDisplay(bool bInitDisasm)
{
  if (bInitDisasm) {
    InitDisasm();
  }

  if (DebugVideoMode::Instance().IsSet())
  {
    uint32_t mode = 0;
    DebugVideoMode::Instance().Get(&mode);
    VideoRefreshScreen(mode, true);
    return;
  }

  UpdateDisplay( UPDATE_ALL );
}
bool IsDebugSteppingAtFullSpeed(void)
{
  return (g_state.mode == MODE_STEPPING) && g_bDebugFullSpeed;
}

bool g_bDebuggerEatKey = false;

unsigned short g_nDisasmTopAddress = 0;
unsigned short g_nDisasmBotAddress = 0;
unsigned short g_nDisasmCurAddress = 0;

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

const int DEBUGGER_VERSION = MAKE_VERSION(2,9,0,15);

const int WINDOW_DATA_BYTES_PER_LINE = 8;

DebugVideoMode DebugVideoMode::m_Instance;
