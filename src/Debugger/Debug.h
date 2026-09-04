// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Debugger_Breakpoints.h"
#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

// Globals
extern bool g_debugger_eat_key;
extern uint16_t g_break_memory_address;
extern int g_command;
extern std::vector<Command_t> g_sorted_commands;

// Benchmarking
extern uint32_t extbench;
extern bool g_benchmarking;

// Profile
extern bool g_profiling;
extern ProfileOpcode_t g_profile_opcodes[NUM_OPCODES];
extern ProfileOpmode_t g_profile_opmodes[NUM_OPMODES];
extern uint64_t g_profile_begin_cycles;
extern const std::string g_file_name_profile;
extern int g_profile_line_count;
extern char g_profile_line[NUM_PROFILE_LINES][CONSOLE_WIDTH];

void ProfileReset();
bool ProfileSave();
void ProfileFormat(bool bSeperateColumns, int eFormatMode);
char* ProfileLinePeek(int iLine);
char* ProfileLinePush();
void ProfileLineReset();

void DisasmCalcTopBotAddress();

// Window
extern int g_console_display_lines;
extern bool g_console_full_width;
extern int g_console_display_width;
extern int g_disasm_win_height;
extern int g_disasm_cur_line;

void WindowUpdateDisasmSize();
void WindowUpdateConsoleDisplayedSize();
void WindowUpdateSizes();
int WindowGetHeight(int iWindow);

char FormatChar4Font(const uint8_t b, bool* pWasHi_, bool* pWasLo_);

extern int g_debug_steps;
extern uint32_t g_debug_step_cycles;
extern int g_debug_step_start;
extern int g_debug_step_until;
extern int g_debug_skip_start;
extern int g_debug_skip_len;

extern bool g_debug_full_speed;
extern bool g_last_go_cmd_was_full_speed;
extern bool g_go_cmd_reinit_flag;

extern FilePtr_t g_trace_file;
extern bool g_trace_header;
extern bool g_trace_file_with_video_scanner;
extern char g_file_name_trace[];

// Bookmarks
#include "Debugger_Bookmarks.h"

// Breakpoints
enum BreakpointHit_t {
  BP_HIT_NONE = 0,
  BP_HIT_INVALID = (1 << 0),
  BP_HIT_OPCODE = (1 << 1),
  BP_HIT_REG = (1 << 2),
  BP_HIT_MEM = (1 << 3),
  BP_HIT_MEMR = (1 << 4),
  BP_HIT_MEMW = (1 << 5),
  BP_HIT_PC_READ_FLOATING_BUS_OR_IO_MEM = (1 << 6)
};
extern int g_debug_break_on_opcode;

// Commands
extern int g_command;  // last command

extern Command_t g_commands[];
extern Command_t g_parameters[];
extern const int NUM_COMMANDS_WITH_ALIASES;

class commands_functor_compare {
 public:
  bool operator()(const Command_t& rLHS, const Command_t& rRHS) const {
    // return true if lhs<rhs
    return (strcmp(rLHS.name, rRHS.name) <= 0);
  }
};

// Config - FileName
extern std::string g_file_name_config;

// Cursor
extern uint16_t g_disasm_top_address;
extern uint16_t g_disasm_bot_address;
extern uint16_t g_disasm_cur_address;

extern bool g_disasm_cur_bad;
extern int g_disasm_cur_line;  // Aligned to Top or Center
extern int g_disasm_cur_state;

extern int g_disasm_win_height;

extern const int WINDOW_DATA_BYTES_PER_LINE;

// Config - Disassembly
extern bool g_config_disasm_address_view;
extern int g_config_disasm_click;  // GH#462
extern bool g_config_disasm_address_colon;
extern bool g_config_disasm_opcodes_view;
extern bool g_config_disasm_opcode_spaces;
extern int g_config_disasm_targets;
extern int g_config_disasm_branch_type;
extern int g_config_disasm_immediate_char;

// Config - info
extern bool g_config_info_target_pointer;

// Font
extern int g_font_height;
extern int g_font_spacing;

// Memory
#include "Debugger_Memory.h"

// Source Level Debugging
extern std::string g_source_file_name;
extern MemoryTextFile_t g_assembler_source_buffer;

extern int g_source_display_start;
extern int g_source_assemble_bytes;
extern int g_source_assembly_symbols;

// Version
extern const int DEBUGGER_VERSION;

// Watches
extern int g_watches_count;
extern Watches_t g_watches[MAX_WATCHES];

// Window
extern int g_window_last;
extern int g_window_this;
extern WindowSplit_t g_window_config[NUM_WINDOWS];

// Zero Page
extern int g_zero_page_pointers_count;
extern ZeroPagePointers_t
    g_zero_page_pointers[MAX_ZEROPAGE_POINTERS];  // TODO: use vector<> ?

// Prototypes

// Bookmarks
bool Bookmark_Find(const uint16_t address);

// Breakpoints
bool GetBreakpointInfo(uint16_t nOffset, bool& bBreakpointActive_,
                       bool& bBreakpointEnable_);

// Color
uint32_t DebuggerGetColor(int iColor);

// Source Level Debugging
int FindSourceLine(uint16_t address);

const char* FormatAddress(uint16_t address, int nBytes);

// Symbol Table / Memory
bool FindAddressFromSymbol(const char* pSymbol, uint16_t* pAddress_ = nullptr,
                           int* iTable_ = nullptr);

uint16_t GetAddressFromSymbol(
    const char* symbol);  // HACK: returns 0 if symbol not found
void SymbolUpdate(SymbolTable_Index_e eSymbolTable, const char* pSymbolName,
                  uint16_t nAddrss, bool bRemoveSymbol, bool bUpdateSymbol);

const char* FindSymbolFromAddress(uint16_t nAdress, int* iTable_ = nullptr);

const char* GetSymbol(uint16_t address, int nBytes);

// DebugVideoMode _____________________________________________________________

// Fix for GH#345
// Wrap & protect the debugger's video mode in its own class:
// . This may seem like overkill but it stops the video mode being (erroneously)
// additionally used as a flag. . VideoMode is a bitmap of video flags and a
// VideoMode value of zero is a valid video mode (GR,PAGE1,non-mixed).
class DebugVideoMode  // NB. Implemented as a singleton
{
 protected:
  DebugVideoMode() { Reset(); }

 public:
  ~DebugVideoMode() = default;

  static auto Instance() -> DebugVideoMode& { return instance_; }

  auto Reset() -> void {
    is_video_mode_valid_ = false;
    video_mode_ = 0;
  }

  auto IsSet() const -> bool { return is_video_mode_valid_; }

  auto Get(uint32_t* video_mode_out) const -> bool {
    if (video_mode_out != nullptr) {
      *video_mode_out = is_video_mode_valid_ ? video_mode_ : 0;
    }
    return is_video_mode_valid_;
  }

  auto Set(uint32_t video_mode) -> void {
    is_video_mode_valid_ = true;
    video_mode_ = video_mode;
  }

 private:
  bool is_video_mode_valid_{false};
  uint32_t video_mode_{0};

  static DebugVideoMode instance_;
};

auto DebuggerProcessCommand(bool echo_console_input) -> Update_t;

void UpdateDisplay(Update_t bUpdate);

// Prototypes
extern const int DEBUGGER_VERSION;

enum {
  DEBUG_EXIT_KEY = 0x1B,  // Escape
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
bool GetBreakpointInfo(uint16_t nOffset, bool& bBreakpointActive_,
                       bool& bBreakpointEnable_);

void DebuggerRunScript(const char* sFileName);

void DebugContinueStepping(const bool bCallerWillUpdateDisplay = false);

void debug_destroy();

void debug_display(bool bInitDisasm = false);

void debug_end();

void debug_initialize();

// Cursor/Input
extern bool g_input_cursor_visible;
extern int g_input_cursor_index;
extern const char g_input_cursor[];
extern bool g_console_input_quoted;
extern int g_console_input_skip;
extern bool g_ignore_next_key;

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
