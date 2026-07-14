#include <cstdint>
#pragma once

#include <vector>
#include <algorithm> // sort
#include <map>
#include <string>

using namespace std;

#include <cstring>
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"
#include "Debugger_Breakpoints.h"

// Globals
extern bool g_debugger_eat_key;
extern uint16_t g_uBreakMemoryAddress;
extern int g_iCommand;
extern std::vector<Command_t> g_vSortedCommands;

// Benchmarking
extern uint32_t extbench;
extern bool g_bBenchmarking;

// Profile
extern bool g_bProfiling;
extern ProfileOpcode_t g_aProfileOpcodes[NUM_OPCODES];
extern ProfileOpmode_t g_aProfileOpmodes[NUM_OPMODES];
extern uint64_t g_nProfileBeginCycles;
extern const std::string g_FileNameProfile;
extern int g_nProfileLine;
extern char g_aProfileLine[NUM_PROFILE_LINES][CONSOLE_WIDTH];

void ProfileReset();
bool ProfileSave();
void ProfileFormat(bool bSeperateColumns, int eFormatMode);
char* ProfileLinePeek(int iLine);
char* ProfileLinePush();
void ProfileLineReset();

void DisasmCalcTopBotAddress();

// Window
extern int g_nConsoleDisplayLines;
extern bool g_bConsoleFullWidth;
extern int g_nConsoleDisplayWidth;
extern int g_nDisasmWinHeight;
extern int g_nDisasmCurLine;

void WindowUpdateDisasmSize();
void WindowUpdateConsoleDisplayedSize();
void WindowUpdateSizes();
int  WindowGetHeight(int iWindow);

char FormatChar4Font(const uint8_t b, bool *pWasHi_, bool *pWasLo_);

extern int g_nDebugSteps;
extern uint32_t g_nDebugStepCycles;
extern int g_nDebugStepStart;
extern int g_nDebugStepUntil;
extern int g_nDebugSkipStart;
extern int g_nDebugSkipLen;

extern bool g_bDebugFullSpeed;
extern bool g_bLastGoCmdWasFullSpeed;
extern bool g_bGoCmd_ReinitFlag;

extern FILE *g_hTraceFile;
extern bool g_bTraceHeader;
extern bool g_bTraceFileWithVideoScanner;
extern char g_sFileNameTrace[];

// Bookmarks
#include "Debugger_Bookmarks.h"

// Breakpoints
enum BreakpointHit_t
{
  BP_HIT_NONE = 0,
  BP_HIT_INVALID = (1 << 0),
  BP_HIT_OPCODE = (1 << 1),
  BP_HIT_REG = (1 << 2),
  BP_HIT_MEM = (1 << 3),
  BP_HIT_MEMR = (1 << 4),
  BP_HIT_MEMW = (1 << 5),
  BP_HIT_PC_READ_FLOATING_BUS_OR_IO_MEM = (1 << 6)
};
extern int g_iDebugBreakOnOpcode;

// Commands
extern int g_iCommand; // last command

extern Command_t g_aCommands[];
extern Command_t g_aParameters[];
extern const int NUM_COMMANDS_WITH_ALIASES;

class commands_functor_compare
{
  public:
    bool operator() ( const Command_t & rLHS, const Command_t & rRHS ) const
    {
      // return true if lhs<rhs
      return (strcmp( rLHS.m_sName, rRHS.m_sName ) <= 0);
    }
};

// Config - FileName
extern std::string g_sFileNameConfig;

// Cursor
extern uint16_t g_nDisasmTopAddress;
extern uint16_t g_nDisasmBotAddress;
extern uint16_t g_nDisasmCurAddress;

extern bool g_bDisasmCurBad;
extern int g_nDisasmCurLine; // Aligned to Top or Center
extern int g_iDisasmCurState;

extern int g_nDisasmWinHeight;

extern const int WINDOW_DATA_BYTES_PER_LINE;

// Config - Disassembly
extern bool g_bConfigDisasmAddressView;
extern int  g_bConfigDisasmClick; // GH#462
extern bool g_bConfigDisasmAddressColon;
extern bool g_bConfigDisasmOpcodesView;
extern bool g_bConfigDisasmOpcodeSpaces;
extern int g_iConfigDisasmTargets;
extern int g_iConfigDisasmBranchType;
extern int g_bConfigDisasmImmediateChar;

// Config - Info
extern bool g_bConfigInfoTargetPointer;

// Font
extern int g_nFontHeight;
extern int g_iFontSpacing;

// Memory
#include "Debugger_Memory.h"

// Source Level Debugging
extern std::string g_aSourceFileName;
extern MemoryTextFile_t g_AssemblerSourceBuffer;

extern int g_iSourceDisplayStart;
extern int g_nSourceAssembleBytes;
extern int g_nSourceAssemblySymbols;

// Version
extern const int DEBUGGER_VERSION;

// Watches
extern int g_nWatches;
extern Watches_t g_aWatches[MAX_WATCHES];

// Window
extern int g_iWindowLast;
extern int g_iWindowThis;
extern WindowSplit_t g_aWindowConfig[NUM_WINDOWS];

// Zero Page
extern int g_nZeroPagePointers;
extern ZeroPagePointers_t g_aZeroPagePointers[MAX_ZEROPAGE_POINTERS]; // TODO: use vector<> ?

// Prototypes

// Bookmarks
bool Bookmark_Find(const uint16_t nAddress);

// Breakpoints
bool GetBreakpointInfo(uint16_t nOffset, bool &bBreakpointActive_, bool &bBreakpointEnable_);

// Color
uint32_t DebuggerGetColor(int iColor);

// Source Level Debugging
int FindSourceLine(uint16_t nAddress);

const char* FormatAddress(uint16_t nAddress, int nBytes);

// Symbol Table / Memory
bool FindAddressFromSymbol(const char* pSymbol, uint16_t *pAddress_ = NULL, int *iTable_ = NULL);

uint16_t GetAddressFromSymbol(const char* symbol); // HACK: returns 0 if symbol not found
void SymbolUpdate(SymbolTable_Index_e eSymbolTable, const char *pSymbolName, uint16_t nAddrss, bool bRemoveSymbol, bool bUpdateSymbol);

const char* FindSymbolFromAddress(uint16_t nAdress, int *iTable_ = NULL);

const char* GetSymbol(uint16_t nAddress, int nBytes);

// DebugVideoMode _____________________________________________________________

// Fix for GH#345
// Wrap & protect the debugger's video mode in its own class:
// . This may seem like overkill but it stops the video mode being (erroneously) additionally used as a flag.
// . VideoMode is a bitmap of video flags and a VideoMode value of zero is a valid video mode (GR,PAGE1,non-mixed).
class DebugVideoMode  // NB. Implemented as a singleton
{
protected:
  DebugVideoMode()
  {
    Reset();
  }

public:
  ~DebugVideoMode(){}

  static DebugVideoMode& Instance()
  {
    return m_Instance;
  }

  void Reset(void)
  {
    m_bIsVideoModeValid = false;
    m_uVideoMode = 0;
  }

  bool IsSet(void)
  {
    return m_bIsVideoModeValid;
  }

  bool Get(uint32_t* pVideoMode)
  {
    if (pVideoMode)
      *pVideoMode = m_bIsVideoModeValid ? m_uVideoMode : 0;
    return m_bIsVideoModeValid;
  }

  void Set(uint32_t videoMode)
  {
    m_bIsVideoModeValid = true;
    m_uVideoMode = videoMode;
  }

private:
  bool m_bIsVideoModeValid;
  uint32_t m_uVideoMode;

  static DebugVideoMode m_Instance;
};

Update_t DebuggerProcessCommand(const bool bEchoConsoleInput);

void UpdateDisplay(Update_t bUpdate);

// Prototypes
extern const int DEBUGGER_VERSION;

enum {
  DEBUG_EXIT_KEY = 0x1B, // Escape
  DEBUG_TOGGLE_KEY = LINAPPLE_KEY_F1 + 6
};

Update_t CmdGoNormalSpeed(int nArgs);
Update_t CmdGoFullSpeed(int nArgs);
Update_t CmdKey(int nArgs);
Update_t CmdSync(int nArgs);
Update_t CmdStackPush(int nArgs);
Update_t CmdStackPop(int nArgs);
Update_t CmdStackPopPseudo(int nArgs);
Update_t CmdVideoScannerInfo(int nArgs);
Update_t CmdCyclesInfo(int nArgs);
Update_t CmdFlagClear(int nArgs);
Update_t CmdFlagSet(int nArgs);
Update_t CmdFlag(int nArgs);

Update_t CmdUnassemble(int nArgs);
Update_t CmdDisk(int nArgs);
Update_t CmdSource(int nArgs);
Update_t CmdWatch(int nArgs);
Update_t CmdWatchAdd(int nArgs);
Update_t CmdWatchClear(int nArgs);
Update_t CmdWatchDisable(int nArgs);
Update_t CmdWatchEnable(int nArgs);
Update_t CmdWatchList(int nArgs);
Update_t CmdWatchLoad(int nArgs);
Update_t CmdWatchSave(int nArgs);

void debug_begin();

bool IsDebugBreakOnInvalid(int iOpcodeType);
void SetDebugBreakOnInvalid(int iOpcodeType, int nValue);
int CheckBreakpointsIO();
int CheckBreakpointsReg();
void ClearTempBreakpoints();
bool GetBreakpointInfo(uint16_t nOffset, bool &bBreakpointActive_, bool &bBreakpointEnable_);

void DebuggerRunScript(const char* sFileName);

void DebugContinueStepping(const bool bCallerWillUpdateDisplay=false);

void debug_destroy();

void debug_display(bool bInitDisasm=false);

void debug_end();

void debug_initialize();

// Cursor/Input
extern bool g_bInputCursor;
extern int  g_iInputCursor;
extern const char g_aInputCursor[];
extern bool g_bConsoleInputQuoted;
extern int  g_nConsoleInputSkip;
extern bool g_bIgnoreNextKey;

void DebuggerUpdate();
void DebuggerCursorUpdate();
void DebuggerCursorNext();
void debugger_process_key(int keycode);
void debugger_input_console_char(char ch);
void debugger_mouse_click(int x, int y);
void ToggleFullScreenConsole();

void VerifyDebuggerCommandTable();

bool is_debug_stepping_at_full_speed(void);

bool debug_get_video_mode(uint32_t* pVideoMode);
bool can_draw_debugger(void);
