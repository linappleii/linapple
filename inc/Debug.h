#pragma once

#include <vector>
#include <algorithm> // sort
#include <map>
#include <string>

using namespace std;

#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"

// Globals
extern bool g_bDebuggerEatKey;

// Benchmarking
extern unsigned int extbench;

// Bookmarks
extern int g_nBookmarks;
extern Bookmark_t g_aBookmarks[MAX_BOOKMARKS];

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

extern int g_nBreakpoints;
extern Breakpoint_t g_aBreakpoints[MAX_BREAKPOINTS];

extern const char *g_aBreakpointSource[NUM_BREAKPOINT_SOURCES];
extern const char *g_aBreakpointSymbols[NUM_BREAKPOINT_OPERATORS];

// Full-Speed debugging
extern int g_nDebugOnBreakInvalid;
extern int g_iDebugBreakOnOpcode;

// Commands
extern const int NUM_COMMANDS_WITH_ALIASES; // = sizeof(g_aCommands) / sizeof (Command_t); // Determined at compile-time ;-)
extern int g_iCommand; // last command

extern Command_t g_aCommands[];
extern Command_t g_aParameters[];

class commands_functor_compare
{
  public:
    bool operator() ( const Command_t & rLHS, const Command_t & rRHS ) const
    {
      // return true if lhs<rhs
      return (_tcscmp( rLHS.m_sName, rRHS.m_sName ) <= 0) ? true : false;
    }
};

// Config - FileName
extern std::string g_sFileNameConfig;

// Cursor
extern unsigned short g_nDisasmTopAddress;
extern unsigned short g_nDisasmBotAddress;
extern unsigned short g_nDisasmCurAddress;

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
extern MemoryDump_t g_aMemDump[NUM_MEM_DUMPS];

extern vector<int> g_vMemorySearchResults;

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
bool Bookmark_Find(const unsigned short nAddress);

// Breakpoints
bool GetBreakpointInfo(unsigned short nOffset, bool &bBreakpointActive_, bool &bBreakpointEnable_);

// Color
unsigned int DebuggerGetColor(int iColor);

// Source Level Debugging
int FindSourceLine(unsigned short nAddress);

LPCTSTR FormatAddress(unsigned short nAddress, int nBytes);

// Symbol Table / Memory
bool FindAddressFromSymbol(LPCSTR pSymbol, unsigned short *pAddress_ = NULL, int *iTable_ = NULL);

unsigned short GetAddressFromSymbol(LPCTSTR symbol); // HACK: returns 0 if symbol not found
void SymbolUpdate(SymbolTable_Index_e eSymbolTable, char *pSymbolName, unsigned short nAddrss, bool bRemoveSymbol, bool bUpdateSymbol);

LPCTSTR FindSymbolFromAddress(unsigned short nAdress, int *iTable_ = NULL);

LPCTSTR GetSymbol(unsigned short nAddress, int nBytes);

Update_t DebuggerProcessCommand(const bool bEchoConsoleInput);

// Prototypes

enum {
  DEBUG_EXIT_KEY = 0x1B, // Escape
  DEBUG_TOGGLE_KEY = SDLK_F1 + BTN_DEBUG
};

void DebugBegin();

void DebugContinueStepping(const bool bCallerWillUpdateDisplay=false);

void DebugDestroy();

void DebugDisplay(bool bInitDisasm=false);

void DebugEnd();

void DebugInitialize();

void DebuggerInputConsoleChar(char ch);

void DebuggerProcessKey(int keycode);

void DebuggerUpdate();

void DebuggerCursorNext();

void DebuggerMouseClick(int x, int y);

void VerifyDebuggerCommandTable();

bool IsDebugSteppingAtFullSpeed(void);

bool DebugGetVideoMode(unsigned int* pVideoMode);
