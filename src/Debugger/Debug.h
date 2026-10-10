// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

// Globals
extern bool debugger_eat_key;
extern uint16_t break_memory_address;
extern int command;
extern std::vector<Command> sorted_commands;

// Benchmarking
extern uint32_t extbench;
extern bool benchmarking;

// Profile
extern bool profiling;
extern ProfileOpcode profile_opcodes[NUM_OPCODES];
extern ProfileOpmode profile_opmodes[NUM_OPMODES];
extern uint64_t profile_begin_cycles;
extern const char* const file_name_profile;
extern int profile_line_count;
extern char profile_line[NUM_PROFILE_LINES][CONSOLE_WIDTH];

auto ProfileReset() -> void;
auto ProfileSave() -> bool;
auto ProfileFormat(bool bSeperateColumns, int eFormatMode) -> void;
auto ProfileLinePeek(int iLine) -> char*;
auto ProfileLinePush() -> char*;
auto ProfileLineReset() -> void;

auto DisasmCalcTopBotAddress() -> void;

// Window
extern int console_display_lines;
extern bool console_full_width;
extern int console_display_width;
extern int disasm_win_height;
extern int disasm_cur_line;

auto WindowUpdateDisasmSize() -> void;
auto WindowUpdateConsoleDisplayedSize() -> void;
auto WindowUpdateSizes() -> void;
auto WindowGetHeight(int iWindow) -> int;

auto FormatChar4Font(uint8_t b, bool* pWasHi_, bool* pWasLo_) -> char;

extern int debug_steps;
extern uint32_t debug_step_cycles;
extern int debug_step_start;
extern int debug_step_until;
extern int debug_skip_start;
extern int debug_skip_len;

extern bool debug_full_speed;
extern bool last_go_cmd_was_full_speed;
extern bool go_cmd_reinit_flag;

extern FilePtr trace_file;
extern bool trace_header;
extern bool trace_file_with_video_scanner;
extern char file_name_trace[];

// Bookmarks

// Breakpoints
enum BreakpointHit : uint8_t {
  BP_HIT_NONE = 0,
  BP_HIT_INVALID = (1 << 0),
  BP_HIT_OPCODE = (1 << 1),
  BP_HIT_REG = (1 << 2),
  BP_HIT_MEM = (1 << 3),
  BP_HIT_MEMR = (1 << 4),
  BP_HIT_MEMW = (1 << 5),
  BP_HIT_PC_READ_FLOATING_BUS_OR_IO_MEM = (1 << 6),
};
extern int debug_break_on_opcode;

// Commands

extern Command commands[];
extern Command parameters[];
extern const int NUM_COMMANDS_WITH_ALIASES;

class commands_functor_compare {
 public:
  auto operator()(const Command& rLHS, const Command& rRHS) const -> bool {
    // return true if lhs<rhs
    return (strcmp(rLHS.name, rRHS.name) <= 0);
  }
};

// Config - FileName
extern const char* const file_name_config;

// Cursor
extern uint16_t disasm_top_address;
extern uint16_t disasm_bot_address;
extern uint16_t disasm_cur_address;

extern bool disasm_cur_bad;
// Aligned to Top or Center
extern int disasm_cur_state;

extern const int WINDOW_DATA_BYTES_PER_LINE;

// Config - Disassembly
extern bool config_disasm_address_view;
extern int config_disasm_click;  // GH#462
extern bool config_disasm_address_colon;
extern bool config_disasm_opcodes_view;
extern bool config_disasm_opcode_spaces;
extern int config_disasm_targets;
extern int config_disasm_branch_type;
extern int config_disasm_immediate_char;

// Config - info
extern bool config_info_target_pointer;

// Font
extern int font_height;
extern int font_spacing;

// Memory

// Source Level Debugging
extern std::string source_file_name;
extern MemoryTextFile assembler_source_buffer;

extern int source_display_start;
extern int source_assemble_bytes;
extern int source_assembly_symbols;

// Version
extern const int DEBUGGER_VERSION;

// Watches
extern int watches_count;
extern Watches watches[MAX_WATCHES];

// Window
extern int window_last;
extern int window_this;
extern WindowConfig window_config[NUM_WINDOWS];

// Zero Page
extern int zero_page_pointers_count;
extern ZeroPagePointers
    zero_page_pointers[MAX_ZEROPAGE_POINTERS];  // TODO: use vector<> ?

// Prototypes

// Bookmarks
auto Bookmark_Find(uint16_t address) -> bool;

// Breakpoints
auto GetBreakpointInfo(uint16_t nOffset, bool& bBreakpointActive_,
                       bool& bBreakpointEnable_) -> bool;

// Color
auto DebuggerGetColor(int iColor) -> uint32_t;

// Source Level Debugging
auto FindSourceLine(uint16_t address) -> int;

auto FormatAddress(uint16_t address, int nBytes) -> const char*;

// Symbol Table / Memory
auto FindAddressFromSymbol(const char* pSymbol, uint16_t* pAddress_ = nullptr,
                           int* iTable_ = nullptr) -> bool;

auto GetAddressFromSymbol(const char* symbol)
    -> uint16_t;  // HACK: returns 0 if symbol not found
auto SymbolUpdate(SymbolTable_Index_e eSymbolTable, const char* pSymbolName,
                  uint16_t address, bool bRemoveSymbol, bool bUpdateSymbol)
    -> void;

auto FindSymbolFromAddress(uint16_t address, int* iTable_ = nullptr) -> const
    char*;

auto GetSymbol(uint16_t address, int nBytes) -> const char*;

// DebugVideoMode _____________________________________________________________

// Fix for GH#345
// Wrap & protect the debugger's video mode in its own class:
// . This may seem like overkill but it stops the video mode being (erroneously)
// additionally used as a flag. . VideoMode is a bitmap of video flags and a
// VideoMode value of zero is a valid video mode (GR,PAGE1,non-mixed).
class DebugVideoMode  // NB. Implemented as a singleton
{
 protected:
  DebugVideoMode() noexcept { Reset(); }

 public:
  ~DebugVideoMode() = default;
  DebugVideoMode(const DebugVideoMode&) = delete;
  auto operator=(const DebugVideoMode&) -> DebugVideoMode& = delete;
  DebugVideoMode(DebugVideoMode&&) = delete;
  auto operator=(DebugVideoMode&&) -> DebugVideoMode& = delete;

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

auto DebuggerProcessCommand(bool bEchoConsoleInput) -> UpdateResult;

auto UpdateDisplay(UpdateResult bUpdate) -> void;

// Prototypes

constexpr int DEBUG_EXIT_KEY = 0x1B;  // Escape
constexpr int DEBUG_TOGGLE_KEY = linapple_key_f7;

auto debug_begin() -> void;

auto IsDebugBreakOnInvalid(int iOpcodeType) -> bool;
auto SetDebugBreakOnInvalid(int iOpcodeType, int nValue) -> void;
auto CheckBreakpointsIO() -> int;
auto CheckBreakpointsReg() -> int;
auto ClearTempBreakpoints() -> void;

auto DebuggerRunScript(const char* pFileName) -> void;

auto DebugContinueStepping(bool bCallerWillUpdateDisplay = false) -> void;

auto debug_destroy() -> void;

auto debug_display(bool bInitDisasm = false) -> void;

auto debug_end() -> void;

auto debug_initialize() -> void;

// Cursor/Input
extern bool input_cursor_visible;
extern int input_cursor_index;
extern const char input_cursor[];
extern bool console_input_quoted;
extern int console_input_skip;
extern bool ignore_next_key;

auto DebuggerUpdate() -> void;
auto DebuggerCursorUpdate() -> void;
auto DebuggerCursorNext() -> void;
auto debugger_process_key(int keycode) -> void;
auto debugger_input_console_char(char ch) -> void;
auto debugger_mouse_click(int x, int y) -> void;
auto ToggleFullScreenConsole() -> void;

auto VerifyDebuggerCommandTable() -> void;

auto is_debug_stepping_at_full_speed(void) -> bool;

auto debug_get_video_mode(uint32_t* pVideoMode) -> bool;
auto can_draw_debugger(void) -> bool;
