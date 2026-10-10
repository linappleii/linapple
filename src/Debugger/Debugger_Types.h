// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "apple2/CPU.h"

// Addressing

constexpr int MAX_OPMODE_FORMAT = 12;
constexpr int MAX_OPMODE_NAME = 32;
constexpr int NO_6502_TARGET = -1;
constexpr int DBG_6502_NUM_FLAGS = 8;
constexpr int CONSOLE_WIDTH = 80;

enum RangeType : uint8_t {
  RANGE_MISSING_ARG_2 = 0,  // error
  RANGE_HAS_LEN,            // valid case 1
  RANGE_HAS_END,            // valid case 2
};

struct AddressingMode {
  char format[MAX_OPMODE_FORMAT];
  int bytes;
  char name[MAX_OPMODE_NAME];
};

/*
    +---------------------+--------------------------+
    | Opmode  e           |     assembler format     |
    +=====================+==========================+
    | Immediate           |          #aa             |
    | Absolute            |          aaaa            |
    | Zero Page           |          aa              |   Note:
    | Implied             |                          |
    | Indirect Absolute   |          (aaaa)          |     aa = 2 hex digits
    | Absolute Indexed,X  |          aaaa,X          |          as $FF
    | Absolute Indexed,Y  |          aaaa,Y          |
    | Zero Page Indexed,X |          aa,X            |     aaaa = 4 hex
    | Zero Page Indexed,Y |          aa,Y            |          digits as
    | Indexed Indirect    |          (aa,X)          |          $FFFF
    | Indirect Indexed    |          (aa),Y          |
    | Relative            |          aaaa            |     Can also be
    | Accumulator         |          A               |     assembler labels
    +---------------------+--------------------------+
    (Table 2-3. _6502 Software Design_, Scanlon, 1980)

Opcode: opc aaa od
  opc...od = Mnemonic / Opcode
  ...aaa.. = Addressing mode
od = 00
  000  #Immediate
  001  Zero page
  011  Absolute
  101  Zero page,X
  111  Absolute,X
od = 01
  000  (Zero page,X)
  001  Zero page
  010  #Immediate
  011  Absolute
  100  (Zero page),Y
  101  Zero page,X
  110  Absolute,Y
  111  Absolute,X
od = 10
  000  #Immediate
  001  Zero page
  010  Accumulator
  011  Absolute
  101  Zero page,X
  111  Absolute,X
*/
/*
  Legend:
    A = Absolute (fortunately Accumulator is implicit, leaving us to use 'A')
    I = Indexed  ( would of been X, but need reg X)
    M = iMmediate
    N = iNdirect
    R = Relative
    X = Offset X Register
    Y = Offset Y Register
    Z = Zeropage
*/
enum AddressingModeId : uint8_t {  // ADDRESSING_MODES_e
  AM_IMPLIED  // Note: SetDebugBreakOnInvalid() assumes this order of first 4
              // entries
  ,
  AM_1  //    Invalid 1 Byte
  ,
  AM_2  //    Invalid 2 Bytes
  ,
  AM_3  //    Invalid 3 Bytes
  ,
  AM_M  //  4 #Immediate
  ,
  AM_A  //  5 $Absolute
  ,
  AM_Z  //  6 Zeropage
  ,
  AM_AX  //  7 Absolute, X
  ,
  AM_AY  //  8 Absolute, Y
  ,
  AM_ZX  //  9 Zeropage, X
  ,
  AM_ZY  // 10 Zeropage, Y
  ,
  AM_R  // 11 Relative
  ,
  AM_IZX  // 12 Indexed (Zeropage Indirect, X)
  ,
  AM_IAX  // 13 Indexed (Absolute Indirect, X)
  ,
  AM_NZY  // 14 Indirect (Zeropage) Indexed, Y
  ,
  AM_NZ  // 15 Indirect (Zeropage)
  ,
  AM_NA  // 16 Indirect (Absolute) i.e. JMP
  ,
  AM_DATA  // Not an opcode! Markup as data
  ,
  NUM_ADDRESSING_MODES,
  NUM_OPMODES = NUM_ADDRESSING_MODES,
  AM_INDIRECT = NUM_ADDRESSING_MODES,  // for assembler
};

// Assembler
enum Prompt : uint8_t { PROMPT_COMMAND, PROMPT_ASSEMBLER, NUM_PROMPTS };

// raised from 13 to 31 for Contiki
constexpr int MAX_SYMBOLS_LEN = 31;

// Bookmarks
constexpr int MAX_BOOKMARKS = 10;

// Breakpoints
constexpr int MAX_BREAKPOINTS = 16;

/*
  Breakpoints are now in a tri-state.
  This allows one to set a bunch of breakpoints, and re-enable the ones you want
  without having to remember which addresses you previously added. :-)

  The difference between Set and Enabled breakpoints:

    Set  Enabled  Break?
    x    x        yes, listed as full brightness
    x    -        no, listed as dimmed
    -    ?        no, not listed
*/
// NOTE: Order must match PARAM_REGS_*
// NOTE: Order must match Breakpoint_Source_t
// NOTE: Order must match breakpoint_source
enum BreakpointSource : uint8_t {
  BP_SRC_REG_A,
  BP_SRC_REG_X,
  BP_SRC_REG_Y,

  BP_SRC_REG_PC,  // Program Counter
  BP_SRC_REG_S,   // Stack Counter

  BP_SRC_REG_P,   // Processor Status
  BP_SRC_FLAG_C,  // Carry
  BP_SRC_FLAG_Z,  // Zero
  BP_SRC_FLAG_I,  // Interrupt
  BP_SRC_FLAG_D,  // Decimal
  BP_SRC_FLAG_B,  // Break
  BP_SRC_FLAG_R,  // Reserved
  BP_SRC_FLAG_V,  // Overflow
  BP_SRC_FLAG_N,  // Sign

  BP_SRC_OPCODE,
  BP_SRC_MEM_RW,
  BP_SRC_MEM_READ_ONLY,
  BP_SRC_MEM_WRITE_ONLY,

  NUM_BREAKPOINT_SOURCES,
};

// Note: Order must match Breakpoint_Operator_t
// Note: Order must match PARAM_BREAKPOINT_*
// Note: Order must match breakpoint_symbols
enum BreakpointOperator : uint8_t {
  BP_OP_LESS_EQUAL,     // <= REG
  BP_OP_LESS_THAN,      // <  REG
  BP_OP_EQUAL,          // =  REG
  BP_OP_NOT_EQUAL,      // != REG
  BP_OP_GREATER_THAN,   // >  REG
  BP_OP_GREATER_EQUAL,  // >= REG
  BP_OP_READ,           // @  MEM @ ? *
  BP_OP_WRITE,          // *  MEM @ ? *
  BP_OP_READ_WRITE,     // ?  MEM @ ? *
  NUM_BREAKPOINT_OPERATORS,
};

struct Breakpoint {
  uint16_t address;  // for registers, functions as nValue
  uint16_t nLength;
  BreakpointSource eSource;
  BreakpointOperator eOperator;
  bool bSet;  // used to be called enabled pre 2.0
  bool bEnabled;
  bool bTemp;  // If true then remove BP when hit or stepping cancelled (eg. G
               // xxxx)
};

struct BreakpointInfo {
  bool bActive;
  bool bEnabled;
  bool bFound;
};

using Bookmark = Breakpoint;
using Watches = Breakpoint;
using ZeroPagePointers = Breakpoint;

// Config

enum ConfigSave : uint8_t {
  CONFIG_SAVE_FILE_CREATE,
  CONFIG_SAVE_FILE_APPEND,
};

// Commands

enum UpdateId : int16_t {
  UPDATE_NOTHING,
  UPDATE_BACKGROUND = (1 << 0),
  UPDATE_BREAKPOINTS = (1 << 1),
  UPDATE_CONSOLE_DISPLAY = (1 << 2),
  UPDATE_CONSOLE_INPUT = (1 << 3),
  UPDATE_DISASM = (1 << 4),
  UPDATE_FLAGS = (1 << 5),
  UPDATE_MEM_DUMP = (1 << 6),
  UPDATE_REGS = (1 << 7),
  UPDATE_STACK = (1 << 8),
  UPDATE_SYMBOLS = (1 << 9),
  UPDATE_TARGETS = (1 << 10),
  UPDATE_WATCH = (1 << 11),
  UPDATE_ZERO_PAGE = (1 << 12),
  UPDATE_SOFTSWITCHES = (1 << 13),
  UPDATE_VIDEOSCANNER = (1 << 14),
  UPDATE_ALL = -1,
};

using UpdateResult = int;

constexpr int MAX_COMMAND_LEN = 12;
constexpr int MAX_ARGS = 32;
constexpr int ARG_SYNTAX_ERROR = -1;
constexpr int MAX_ARG_LEN = 56;  // was 12, extended to allow font names

// NOTE: All Commands return flags of what needs to be redrawn
using CmdFuncPtr = UpdateResult (*)(int);

struct Command {
  const char* name;
  CmdFuncPtr function;
  int command_id;            // offset (enum) for direct command name lookup
  const char* help_summary;  // 1 line help summary
};

// Commands sorted by Category
// NOTE: Commands and commands[] order _MUST_ match !!! Aliases are listed
// at the end
enum Commands : uint8_t {
  // Assembler
  CMD_ASSEMBLE,
  // CPU
  CMD_CURSOR_JUMP_PC,  // Shift
  CMD_CURSOR_SET_PC,   // Ctrl
  CMD_GO_NORMAL_SPEED,
  CMD_GO_FULL_SPEED,
  CMD_IN,
  CMD_INPUT_KEY,
  CMD_JSR,
  CMD_NOP,
  CMD_OUT,
  // CPU - Meta info
  CMD_PROFILE,
  CMD_REGISTER_SET,
  // CPU - Stack
  CMD_STACK_POP,
  CMD_STACK_POP_PSEUDO,
  CMD_STACK_PUSH,
  CMD_STEP_OVER,
  CMD_STEP_OUT,
  CMD_TRACE,
  CMD_TRACE_FILE,
  CMD_TRACE_LINE,
  CMD_UNASSEMBLE,
  // Bookmarks
  CMD_BOOKMARK,
  CMD_BOOKMARK_ADD,
  CMD_BOOKMARK_CLEAR,
  CMD_BOOKMARK_LIST,
  CMD_BOOKMARK_GOTO,
  CMD_BOOKMARK_SAVE,
  // Breakpoints
  CMD_BREAK_INVALID,
  CMD_BREAK_OPCODE,
  CMD_BREAKPOINT,
  CMD_BREAKPOINT_ADD_SMART,  // smart breakpoint
  CMD_BREAKPOINT_ADD_REG,    // break on: PC == Address (fetch/execute)
  CMD_BREAKPOINT_ADD_PC,     // alias BPX = BA
  CMD_BREAKPOINT_ADD_IO,     // break on: [$C000-$C7FF] Load/Store
  CMD_BREAKPOINT_ADD_MEM,    // break on: [$0000-$FFFF], excluding IO
  CMD_BREAKPOINT_ADD_MEMR,   // break on read on: [$0000-$FFFF], excluding IO
  CMD_BREAKPOINT_ADD_MEMW,   // break on write on: [$0000-$FFFF], excluding IO
  CMD_BREAKPOINT_CLEAR,
  CMD_BREAKPOINT_DISABLE,
  CMD_BREAKPOINT_EDIT,
  CMD_BREAKPOINT_ENABLE,
  CMD_BREAKPOINT_LIST,
  CMD_BREAKPOINT_SAVE,
  // Config (debugger settings)
  CMD_BENCHMARK,
  CMD_CONFIG_BW,     // BW    # rr gg bb
  CMD_CONFIG_COLOR,  // COLOR # rr gg bb
  CMD_CONFIG_DISASM,
  CMD_CONFIG_FONT,
  CMD_CONFIG_HCOLOR,  // TODO Video :: SETFRAMECOLOR(#,R,G,B)
  CMD_CONFIG_LOAD,
  CMD_CONFIG_MONOCHROME,  // MONO  # rr gg bb
  CMD_CONFIG_SAVE,
  CMD_CONFIG_GET_DEBUG_DIR,
  CMD_CONFIG_SET_DEBUG_DIR,
  // Cursor
  CMD_CURSOR_JUMP_RET_ADDR,
  CMD_CURSOR_LINE_UP,      // Smart Line Up
  CMD_CURSOR_LINE_UP_1,    // Shift
  CMD_CURSOR_LINE_DOWN,    // Smart Line Down
  CMD_CURSOR_LINE_DOWN_1,  // Shift
  CMD_CURSOR_PAGE_UP,
  CMD_CURSOR_PAGE_UP_256,  // up to nearest page boundary
  CMD_CURSOR_PAGE_UP_4K,   // Up to nearest 4K boundary
  CMD_CURSOR_PAGE_DOWN,
  CMD_CURSOR_PAGE_DOWN_256,  // Down to nearest page boundary
  CMD_CURSOR_PAGE_DOWN_4K,   // Down to nearest 4K boundary
  // Cycles info
  CMD_CYCLES_INFO,
  // Disassembler Data
  CMD_DISASM_DATA,
  CMD_DISASM_CODE,
  CMD_DISASM_LIST,
  CMD_DEFINE_DATA_BYTE1,  // DB $00,$04,$08,$0C,$10,$14,$18,$1C
  CMD_DEFINE_DATA_BYTE2,
  CMD_DEFINE_DATA_BYTE4,
  CMD_DEFINE_DATA_BYTE8,

  CMD_DEFINE_DATA_WORD1,  // DW $300
  CMD_DEFINE_DATA_WORD2,
  CMD_DEFINE_DATA_WORD4,
  CMD_DEFINE_DATA_STR,
  //    , CMD_DEFINE_DATA_FACP // FAC Packed
  //    , CMD_DEFINE_DATA_FACU // FAC Unpacked
  //    , CMD_DATA_DEFINE_ADDR_BYTE_L  // DB< address symbol
  //    , CMD_DATA_DEFINE_ADDR_BYTE_H  // DB> address symbol
  CMD_DEFINE_ADDR_WORD,  // .DA address symbol
  // Disk
  CMD_DISK,
  // Flags - CPU
  CMD_FLAG_CLEAR,  // Flag order must match flag_names CZIDBRVN
  CMD_FLAG_CLR_C,  // 8
  CMD_FLAG_CLR_Z,  // 7
  CMD_FLAG_CLR_I,  // 6
  CMD_FLAG_CLR_D,  // 5
  CMD_FLAG_CLR_B,  // 4
  CMD_FLAG_CLR_R,  // 3
  CMD_FLAG_CLR_V,  // 2
  CMD_FLAG_CLR_N,  // 1
  CMD_FLAG_SET,    // Flag order must match flag_names CZIDBRVN
  CMD_FLAG_SET_C,  // 8
  CMD_FLAG_SET_Z,  // 7
  CMD_FLAG_SET_I,  // 6
  CMD_FLAG_SET_D,  // 5
  CMD_FLAG_SET_B,  // 4
  CMD_FLAG_SET_R,  // 3
  CMD_FLAG_SET_V,  // 2
  CMD_FLAG_SET_N,  // 1
  // Help
  CMD_HELP_LIST,
  CMD_HELP_SPECIFIC,
  CMD_VERSION,
  CMD_MOTD,  // Message of the Day
  // Memory
  CMD_MEMORY_COMPARE,
  CMD_MEM_MINI_DUMP_HEX_1,    // Mini Memory Dump 1
  CMD_MEM_MINI_DUMP_HEX_2,    // Mini Memory Dump 2
  CMD_MEM_MINI_DUMP_ASCII_1,  // ASCII
  CMD_MEM_MINI_DUMP_ASCII_2,
  CMD_MEM_MINI_DUMP_APPLE_1,  // Low-Bit inverse, High-Bit normal
  CMD_MEM_MINI_DUMP_APPLE_2,
  CMD_MEMORY_EDIT,
  CMD_MEMORY_ENTER_BYTE,
  CMD_MEMORY_ENTER_WORD,
  CMD_MEMORY_LOAD,
  CMD_MEMORY_MOVE,
  CMD_MEMORY_SAVE,
  CMD_MEMORY_SEARCH,
  CMD_MEMORY_FIND_RESULTS,
  CMD_MEMORY_SEARCH_HEX,
  CMD_MEMORY_FILL,
  CMD_NTSC,
  CMD_TEXT_SAVE,
  // Output
  CMD_OUTPUT_CALC,
  CMD_OUTPUT_ECHO,
  CMD_OUTPUT_PRINT,
  CMD_OUTPUT_PRINTF,
  CMD_OUTPUT_RUN,
  // Source Level Debugging
  CMD_SOURCE,
  CMD_SYNC,
  // Symbols
  CMD_SYMBOLS_LOOKUP,
  CMD_SYMBOLS_ROM,
  CMD_SYMBOLS_APPLESOFT,
  CMD_SYMBOLS_ASSEMBLY,
  CMD_SYMBOLS_USER_1,
  CMD_SYMBOLS_USER_2,
  CMD_SYMBOLS_SRC_1,
  CMD_SYMBOLS_SRC_2,
  CMD_SYMBOLS_DOS33,
  CMD_SYMBOLS_PRODOS,
  CMD_SYMBOLS_INFO,
  CMD_SYMBOLS_LIST,
  // Video-scanner info
  CMD_VIDEO_SCANNER_INFO,
  // View
  CMD_VIEW_TEXT4X,
  CMD_VIEW_TEXT41,
  CMD_VIEW_TEXT42,
  CMD_VIEW_TEXT8X,
  CMD_VIEW_TEXT81,
  CMD_VIEW_TEXT82,
  CMD_VIEW_GRX,
  CMD_VIEW_GR1,
  CMD_VIEW_GR2,
  CMD_VIEW_DGRX,
  CMD_VIEW_DGR1,
  CMD_VIEW_DGR2,
  CMD_VIEW_HGRX,
  CMD_VIEW_HGR1,
  CMD_VIEW_HGR2,
  CMD_VIEW_DHGRX,
  CMD_VIEW_DHGR1,
  CMD_VIEW_DHGR2,
  // Watch
  CMD_WATCH,
  CMD_WATCH_ADD,
  CMD_WATCH_CLEAR,
  CMD_WATCH_DISABLE,
  CMD_WATCH_ENABLE,
  CMD_WATCH_LIST,
  CMD_WATCH_SAVE,
  // Window
  CMD_WINDOW,
  CMD_WINDOW_CODE,
  CMD_WINDOW_CODE_1,
  CMD_WINDOW_CODE_2,
  CMD_WINDOW_CONSOLE,
  CMD_WINDOW_DATA,
  CMD_WINDOW_DATA_1,
  CMD_WINDOW_DATA_2,

  // SOURCE is reserved for source level debugging
  CMD_WINDOW_SOURCE_1,
  CMD_WINDOW_SOURCE_2,

  CMD_WINDOW_OUTPUT,
  // ZeroPage
  CMD_ZEROPAGE_POINTER,
  CMD_ZEROPAGE_POINTER_0,
  CMD_ZEROPAGE_POINTER_1,
  CMD_ZEROPAGE_POINTER_2,
  CMD_ZEROPAGE_POINTER_3,
  CMD_ZEROPAGE_POINTER_4,
  CMD_ZEROPAGE_POINTER_5,
  CMD_ZEROPAGE_POINTER_6,
  CMD_ZEROPAGE_POINTER_7,
  CMD_ZEROPAGE_POINTER_ADD,
  CMD_ZEROPAGE_POINTER_CLEAR,
  CMD_ZEROPAGE_POINTER_DISABLE,
  CMD_ZEROPAGE_POINTER_ENABLE,
  CMD_ZEROPAGE_POINTER_LIST,
  CMD_ZEROPAGE_POINTER_SAVE,
  NUM_COMMANDS,
};

// Assembler
auto CmdAssemble(int nArgs) -> UpdateResult;

// Disassembler Data
auto CmdDisasmDataDefCode(int nArgs) -> UpdateResult;
auto CmdDisasmDataList(int nArgs) -> UpdateResult;

auto CmdDisasmDataDefByte1(int nArgs) -> UpdateResult;
auto CmdDisasmDataDefByte2(int nArgs) -> UpdateResult;
auto CmdDisasmDataDefByte4(int nArgs) -> UpdateResult;
auto CmdDisasmDataDefByte8(int nArgs) -> UpdateResult;

auto CmdDisasmDataDefWord1(int nArgs) -> UpdateResult;
auto CmdDisasmDataDefWord2(int nArgs) -> UpdateResult;
auto CmdDisasmDataDefWord4(int nArgs) -> UpdateResult;

auto CmdDisasmDataDefString(int nArgs) -> UpdateResult;

auto CmdDisasmDataDefAddress8H(int nArgs) -> UpdateResult;
auto CmdDisasmDataDefAddress8L(int nArgs) -> UpdateResult;
auto CmdDisasmDataDefAddress16(int nArgs) -> UpdateResult;

// CPU
auto CmdCursorJumpPC(int nArgs) -> UpdateResult;
auto CmdCursorSetPC(int nArgs) -> UpdateResult;
auto CmdBreakInvalid(int nArgs) -> UpdateResult;  // Breakpoint IFF Full-speed!
auto CmdBreakOpcode(int nArgs) -> UpdateResult;   // Breakpoint IFF Full-speed!
auto CmdGoNormalSpeed(int nArgs) -> UpdateResult;
auto CmdGoFullSpeed(int nArgs) -> UpdateResult;

auto CmdIn(int nArgs) -> UpdateResult;

auto CmdKey(int nArgs) -> UpdateResult;

auto CmdJSR(int nArgs) -> UpdateResult;

auto CmdNOP(int nArgs) -> UpdateResult;

auto CmdOut(int nArgs) -> UpdateResult;

auto CmdStepOver(int nArgs) -> UpdateResult;

auto CmdStepOut(int nArgs) -> UpdateResult;

auto CmdTrace(int nArgs) -> UpdateResult;  // alias for CmdStepIn
auto CmdTraceFile(int nArgs) -> UpdateResult;

auto CmdTraceLine(int nArgs) -> UpdateResult;

auto CmdUnassemble(int nArgs) -> UpdateResult;  // code dump, aka, Unassemble
// Bookmarks
auto CmdBookmark(int nArgs) -> UpdateResult;

auto CmdBookmarkAdd(int nArgs) -> UpdateResult;

auto CmdBookmarkClear(int nArgs) -> UpdateResult;

auto CmdBookmarkList(int nArgs) -> UpdateResult;

auto CmdBookmarkGoto(int nArgs) -> UpdateResult;

auto CmdBookmarkSave(int nArgs) -> UpdateResult;

// Breakpoints
auto CmdBreakpoint(int nArgs) -> UpdateResult;

auto CmdBreakpointAddSmart(int nArgs) -> UpdateResult;

auto CmdBreakpointAddReg(int nArgs) -> UpdateResult;

auto CmdBreakpointAddPC(int nArgs) -> UpdateResult;

auto CmdBreakpointAddIO(int nArgs) -> UpdateResult;

auto CmdBreakpointAddMem(int nArgs, BreakpointSource bpSrc = BP_SRC_MEM_RW)
    -> UpdateResult;

auto CmdBreakpointAddMemA(int nArgs) -> UpdateResult;

auto CmdBreakpointAddMemR(int nArgs) -> UpdateResult;

auto CmdBreakpointAddMemW(int nArgs) -> UpdateResult;

auto CmdBreakpointClear(int nArgs) -> UpdateResult;

auto CmdBreakpointDisable(int nArgs) -> UpdateResult;

auto CmdBreakpointEdit(int nArgs) -> UpdateResult;

auto CmdBreakpointEnable(int nArgs) -> UpdateResult;

auto CmdBreakpointList(int nArgs) -> UpdateResult;

auto CmdBreakpointSave(int nArgs) -> UpdateResult;

// Benchmark
auto CmdBenchmark(int nArgs) -> UpdateResult;
auto CmdBenchmarkStart(int nArgs)
    -> UpdateResult;  // UpdateResult CmdSetupBenchmark (int nArgs);
auto CmdBenchmarkStop(int nArgs)
    -> UpdateResult;  // UpdateResult CmdExtBenchmark (int nArgs);
auto CmdProfile(int nArgs) -> UpdateResult;
auto CmdProfileStart(int nArgs) -> UpdateResult;
auto CmdProfileStop(int nArgs) -> UpdateResult;

// Config
auto CmdConfigColorMono(int nArgs) -> UpdateResult;
auto CmdConfigDisasm(int nArgs) -> UpdateResult;
auto CmdConfigFont(int nArgs) -> UpdateResult;
auto CmdConfigHColor(int nArgs) -> UpdateResult;
auto CmdConfigLoad(int nArgs) -> UpdateResult;
auto CmdConfigSave(int nArgs) -> UpdateResult;
auto CmdConfigSetFont(int nArgs) -> UpdateResult;
auto CmdConfigGetFont(int nArgs) -> UpdateResult;
auto CmdConfigGetDebugDir(int nArgs) -> UpdateResult;
auto CmdConfigSetDebugDir(int nArgs) -> UpdateResult;

// Cursor
auto CmdCursorFollowTarget(int nArgs) -> UpdateResult;
auto CmdCursorLineDown(int nArgs) -> UpdateResult;
auto CmdCursorLineUp(int nArgs) -> UpdateResult;
auto CmdCursorJumpRetAddr(int nArgs) -> UpdateResult;
auto CmdCursorRunUntil(int nArgs) -> UpdateResult;
auto CmdCursorPageDown(int nArgs) -> UpdateResult;
auto CmdCursorPageDown256(int nArgs) -> UpdateResult;
auto CmdCursorPageDown4K(int nArgs) -> UpdateResult;
auto CmdCursorPageUp(int nArgs) -> UpdateResult;
auto CmdCursorPageUp256(int nArgs) -> UpdateResult;
auto CmdCursorPageUp4K(int nArgs) -> UpdateResult;

// Cycles info
auto CmdCyclesInfo(int nArgs) -> UpdateResult;

// Disk
auto CmdDisk(int nArgs) -> UpdateResult;

// Help
auto CmdHelpList(int nArgs) -> UpdateResult;

auto CmdHelpSpecific(int nArgs) -> UpdateResult;

auto CmdVersion(int nArgs) -> UpdateResult;

auto CmdMOTD(int nArgs) -> UpdateResult;

// Flags
auto CmdFlag(int nArgs) -> UpdateResult;

auto CmdFlagClear(int nArgs) -> UpdateResult;

auto CmdFlagSet(int nArgs) -> UpdateResult;

// Memory (Data)
auto CmdMemoryCompare(int nArgs) -> UpdateResult;
auto CmdMemoryMiniDumpHex(int nArgs) -> UpdateResult;
auto CmdMemoryMiniDumpAscii(int nArgs) -> UpdateResult;
auto CmdMemoryMiniDumpApple(int nArgs) -> UpdateResult;
auto CmdMemoryEdit(int nArgs) -> UpdateResult;
auto CmdMemoryEnterByte(int nArgs) -> UpdateResult;
auto CmdMemoryEnterWord(int nArgs) -> UpdateResult;
auto CmdMemoryFill(int nArgs) -> UpdateResult;
auto CmdNTSC(int nArgs) -> UpdateResult;
auto CmdTextSave(int nArgs) -> UpdateResult;
auto CmdMemoryLoad(int nArgs) -> UpdateResult;
auto CmdMemoryMove(int nArgs) -> UpdateResult;
auto CmdMemorySave(int nArgs) -> UpdateResult;
auto CmdMemorySearch(int nArgs) -> UpdateResult;
auto SearchMemoryDisplay(int nArgs = 0) -> UpdateResult;  // TODO: CLEANUP
auto CmdMemorySearchAscii(int nArgs) -> UpdateResult;
auto CmdMemorySearchApple(int nArgs) -> UpdateResult;
auto CmdMemorySearchHex(int nArgs) -> UpdateResult;

// Output/Scripts
auto CmdOutputCalc(int nArgs) -> UpdateResult;

auto CmdOutputEcho(int nArgs) -> UpdateResult;

auto CmdOutputPrint(int nArgs) -> UpdateResult;

auto CmdOutputPrintf(int nArgs) -> UpdateResult;

auto CmdOutputRun(int nArgs) -> UpdateResult;

// Registers
auto CmdRegisterSet(int nArgs) -> UpdateResult;

// Source Level Debugging
auto CmdSource(int nArgs) -> UpdateResult;

auto CmdSync(int nArgs) -> UpdateResult;

// Stack
auto CmdStackPush(int nArgs) -> UpdateResult;

auto CmdStackPop(int nArgs) -> UpdateResult;

auto CmdStackPopPseudo(int nArgs) -> UpdateResult;

auto CmdStackReturn(int nArgs) -> UpdateResult;

// Symbols
auto CmdSymbols(int nArgs) -> UpdateResult;
auto CmdSymbolsClear(int nArgs) -> UpdateResult;
auto CmdSymbolsList(int nArgs) -> UpdateResult;
auto CmdSymbolsLoad(int nArgs) -> UpdateResult;
auto CmdSymbolsInfo(int nArgs) -> UpdateResult;
auto CmdSymbolsMain(int nArgs) -> UpdateResult;
auto CmdSymbolsUser(int nArgs) -> UpdateResult;
auto CmdSymbolsSave(int nArgs) -> UpdateResult;
auto CmdSymbolsCommand(int nArgs) -> UpdateResult;
// UpdateResult CmdSymbolsSource(int nArgs);

// Video-scanner info
auto CmdVideoScannerInfo(int nArgs) -> UpdateResult;

// View
auto CmdViewOutput_Text4X(int nArgs) -> UpdateResult;
auto CmdViewOutput_Text41(int nArgs) -> UpdateResult;
auto CmdViewOutput_Text42(int nArgs) -> UpdateResult;
auto CmdViewOutput_Text8X(int nArgs) -> UpdateResult;
auto CmdViewOutput_Text81(int nArgs) -> UpdateResult;
auto CmdViewOutput_Text82(int nArgs) -> UpdateResult;

auto CmdViewOutput_GRX(int nArgs) -> UpdateResult;
auto CmdViewOutput_GR1(int nArgs) -> UpdateResult;
auto CmdViewOutput_GR2(int nArgs) -> UpdateResult;
auto CmdViewOutput_DGRX(int nArgs) -> UpdateResult;
auto CmdViewOutput_DGR1(int nArgs) -> UpdateResult;
auto CmdViewOutput_DGR2(int nArgs) -> UpdateResult;

auto CmdViewOutput_HGRX(int nArgs) -> UpdateResult;
auto CmdViewOutput_HGR1(int nArgs) -> UpdateResult;
auto CmdViewOutput_HGR2(int nArgs) -> UpdateResult;
auto CmdViewOutput_DHGRX(int nArgs) -> UpdateResult;
auto CmdViewOutput_DHGR1(int nArgs) -> UpdateResult;
auto CmdViewOutput_DHGR2(int nArgs) -> UpdateResult;
// Watch
auto CmdWatch(int nArgs) -> UpdateResult;

auto CmdWatchAdd(int nArgs) -> UpdateResult;

auto CmdWatchClear(int nArgs) -> UpdateResult;

auto CmdWatchDisable(int nArgs) -> UpdateResult;

auto CmdWatchEnable(int nArgs) -> UpdateResult;

auto CmdWatchList(int nArgs) -> UpdateResult;

//  UpdateResult CmdWatchLoad    (int nArgs);
auto CmdWatchSave(int nArgs) -> UpdateResult;

// Window
auto CmdWindow(int nArgs) -> UpdateResult;

auto CmdWindowCycleNext(int nArgs) -> UpdateResult;

auto CmdWindowCyclePrev(int nArgs) -> UpdateResult;

auto CmdWindowLast(int nArgs) -> UpdateResult;

auto CmdWindowShowCode(int nArgs) -> UpdateResult;

auto CmdWindowShowCode1(int nArgs) -> UpdateResult;

auto CmdWindowShowCode2(int nArgs) -> UpdateResult;

auto CmdWindowShowData(int nArgs) -> UpdateResult;

auto CmdWindowShowData1(int nArgs) -> UpdateResult;

auto CmdWindowShowData2(int nArgs) -> UpdateResult;

auto CmdWindowShowSymbols1(int nArgs) -> UpdateResult;

auto CmdWindowShowSymbols2(int nArgs) -> UpdateResult;

auto CmdWindowShowSource(int nArgs) -> UpdateResult;

auto CmdWindowShowSource1(int nArgs) -> UpdateResult;

auto CmdWindowShowSource2(int nArgs) -> UpdateResult;

auto CmdWindowViewCode(int nArgs) -> UpdateResult;

auto CmdWindowViewConsole(int nArgs) -> UpdateResult;

auto CmdWindowViewData(int nArgs) -> UpdateResult;

auto CmdWindowViewOutput(int nArgs) -> UpdateResult;

auto CmdWindowViewSource(int nArgs) -> UpdateResult;

auto CmdWindowViewSymbols(int nArgs) -> UpdateResult;

auto CmdWindowWidthToggle(int nArgs) -> UpdateResult;

// ZeroPage
auto CmdZeroPage(int nArgs) -> UpdateResult;

auto CmdZeroPageAdd(int nArgs) -> UpdateResult;

auto CmdZeroPageClear(int nArgs) -> UpdateResult;

auto CmdZeroPageDisable(int nArgs) -> UpdateResult;

auto CmdZeroPageEnable(int nArgs) -> UpdateResult;

auto CmdZeroPageList(int nArgs) -> UpdateResult;

auto CmdZeroPageSave(int nArgs) -> UpdateResult;

auto CmdZeroPagePointer(int nArgs) -> UpdateResult;

// Cursor
enum Cursor_Align_e : uint8_t { CURSOR_ALIGN_TOP, CURSOR_ALIGN_CENTER };

enum CursorHiLightState : uint8_t {
  CURSOR_NORMAL,      // White
  CURSOR_CPU_PC,      // Yellow
  CURSOR_BREAKPOINT,  // Red
};

// Disassembly

// Data Disassembler
enum Nopcode : uint8_t {
  NOP_REMOVED,
  NOP_BYTE_1  // 1 bytes/line
  ,
  NOP_BYTE_2  // 2 bytes/line
  ,
  NOP_BYTE_4  // 4 bytes/line
  ,
  NOP_BYTE_8  // 8 bytes/line
  ,
  NOP_WORD_1  // 1 words/line = 2 bytes (no symbol lookup)
  ,
  NOP_WORD_2  // 2 words/line = 4 bytes
  ,
  NOP_WORD_4  // 4 words/line = 8 bytes
  ,
  NOP_ADDRESS  // 1 word/line  = 2 bytes (with symbol lookup)
  ,
  NOP_HEX  // hex string   =16 bytes
  ,
  NOP_CHAR  // char string // TODO: FIXME: needed??
  ,
  NOP_STRING_ASCII  // Low Ascii
  ,
  NOP_STRING_APPLE  // High Ascii
  ,
  NOP_STRING_APPLESOFT  // Mixed Low/High
  ,
  NOP_FAC,
  NOP_SPRITE,
  NUM_NOPCODE_TYPES,
};

// Disassembler Data
// type symbol[start:end]
struct DisasmData {
  char sSymbol[MAX_SYMBOLS_LEN + 1];

  Nopcode eElementType;  // eElementType -> iNoptype
  int iDirective;          // iDirective   -> iNopcode

  uint16_t nStartAddress;  // link to block [start,end)
  uint16_t nEndAddress;
  uint16_t nArraySize;  // Total bytes
  //  uint16_t nBytePerRow  ; // 1, 8

  // with symbol lookup
  char bSymbolLookup;
  uint16_t nTargetAddress;

  uint16_t nSpriteW;
  uint16_t nSpriteH;
};

enum DisasmBranch : uint8_t {
  DISASM_BRANCH_OFF = 0,
  DISASM_BRANCH_PLAIN,
  DISASM_BRANCH_FANCY,
  NUM_DISASM_BRANCH_TYPES,
};

enum DisasmFormat : uint8_t {
  DISASM_FORMAT_CHAR = (1 << 0),
  DISASM_FORMAT_SYMBOL = (1 << 1),
  DISASM_FORMAT_OFFSET = (1 << 2),
  DISASM_FORMAT_BRANCH = (1 << 3),
  DISASM_FORMAT_TARGET_POINTER = (1 << 4),
  DISASM_FORMAT_TARGET_VALUE = (1 << 5),
};

enum DisasmImmediate : uint8_t {
  DISASM_IMMED_OFF = 0,
  DISASM_IMMED_TARGET,
  DISASM_IMMED_MODE,
  DISASM_IMMED_BOTH,
  NUM_DISASM_IMMED_TYPES,
};

enum DisasmTargets : uint8_t {
  DISASM_TARGET_OFF = 0,
  DISASM_TARGET_VAL,   // Note: Also treated as bit flag !!
  DISASM_TARGET_ADDR,  // Note: Also treated as bit flag !!
  DISASM_TARGET_BOTH,  // Note: Also treated as bit flag !!
  NUM_DISASM_TARGET_TYPES,
};

constexpr int MAX_ADDRESS_LEN = 40;
constexpr int MAX_OPCODES =
    3;  // only display 3 opcode bytes -- See FormatOpcodeBytes() //
        // TODO: FIX when showing data hex
constexpr int CHARS_FOR_ADDRESS = 8;   // 4 digits + end-of-string + padding
constexpr int MAX_IMMEDIATE_LEN = 20;  // Data Disassembly
constexpr int MAX_TARGET_LEN =
    MAX_IMMEDIATE_LEN;  // Debugger Display: pTarget = line.sTarget

struct DisasmLine {
  int opcode;
  int iOpmode;
  int nOpbyte;

  char sAddress[CHARS_FOR_ADDRESS];
  char sOpCodes[(MAX_OPCODES * 3) + 1];

  // Added for Data Disassembler
  char sLabel[MAX_SYMBOLS_LEN + 1];  // label is a symbol

  Nopcode iNoptype;  // basic element type
  int iNopcode;        // assembler directive / pseudo opcode
  int nSlack;

  char sMnemonic[MAX_SYMBOLS_LEN +
                 1];  // either the real Mnemonic or the Assembler Directive
  const DisasmData*
      pDisasmData;  // If != nullptr then bytes are marked up as data not code
  //

  int nTarget;  // address -> string
  char sTarget[MAX_ADDRESS_LEN];

  char sTargetOffset[CHARS_FOR_ADDRESS + 3];  // +/- 255, realistically +/-1
  int nTargetOffset;

  char sTargetPointer[CHARS_FOR_ADDRESS];
  char sTargetValue[CHARS_FOR_ADDRESS];
  //  char sTargetAddress[ CHARS_FOR_ADDRESS ];

  char sImmediate[4];  // 'c'
  char nImmediate;
  char sBranch[4];  // ^

  bool bTargetImmediate;
  bool bTargetIndirect;
  bool bTargetIndexed;
  bool bTargetRelative;
  bool bTargetX;
  bool bTargetY;
  bool bTargetValue;

  auto Clear() -> void {
    sAddress[0] = 0;
    sOpCodes[0] = 0;

    nTarget = 0;
    sTarget[0] = 0;

    sTargetOffset[0] = 0;
    nTargetOffset = 0;

    sTargetPointer[0] = 0;
    sTargetValue[0] = 0;

    sImmediate[0] = 0;
    nImmediate = 0;

    sBranch[0] = 0;
    pDisasmData = nullptr;

    bTargetImmediate = false;
    bTargetIndexed = false;
    bTargetIndirect = false;
    bTargetRelative = false;
    bTargetX = false;
    bTargetY = false;  // need to dislay ",Y"
    bTargetValue = false;
  }
};

// Font
enum FontType : uint8_t {
  FONT_INFO,
  FONT_CONSOLE,
  FONT_DISASM_DEFAULT,
  FONT_DISASM_BRANCH,
  NUM_FONTS,
};

constexpr int MAX_FONT_NAME = MAX_ARG_LEN;

enum FontSpacing : uint8_t {
  FONT_SPACING_CLASSIC,     // least lines (most spacing)
  FONT_SPACING_CLEAN,       // more lines (minimal spacing)
  FONT_SPACING_COMPRESSED,  // max lines (least spacing)
  NUM_FONT_SPACING,
};

struct FontConfig {
  char font_name[MAX_FONT_NAME];
  int font_width_avg;
  int font_width_max;
  int font_height;
  int line_height;  // may or may not include spacer
};

// Instructions / Opcodes

enum MemoryAccess : uint8_t {
  MEM_R = (1 << 0),   // Read
  MEM_W = (1 << 1),   // Write
  MEM_RI = (1 << 2),  // Read Implicit (Implied)
  MEM_WI = (1 << 3),  // Write Implicit (Implied)
  MEM_S = (1 << 4),   // Stack (Read/Write)
  MEM_IM = (1 << 5),  // Immediate - Technically reads target byte

  NUM_MEM_ACCESS,

  // Alias
  MEM_READ = (1 << 0),
  MEM_WRITE = (1 << 1),
};

constexpr int NUM_OPCODES = 256;
constexpr int MAX_MNEMONIC_LEN = 3;

struct Opcodes {
  char sMnemonic[MAX_MNEMONIC_LEN + 1];
  // int16 for structure 8-byte alignment
  int16_t nAddressMode;  // TODO/FIX: nOpmode
  int16_t nMemoryAccess;
};

struct Instruction2 {
  char sMnemonic[MAX_MNEMONIC_LEN + 1];
  int nAddressMode;
  int iMemoryAccess;
};

enum Opcode : uint8_t {
  OPCODE_BRA = 0x80,
  OPCODE_BRK = 0x00,
  OPCODE_JSR = 0x20,
  OPCODE_RTI = 0x40,
  OPCODE_JMP_A = 0x4C,  // Absolute
  OPCODE_RTS = 0x60,
  OPCODE_JMP_NA = 0x6C,   // Indirect Absolute
  OPCODE_JMP_IAX = 0x7C,  // Indexed (Absolute Indirect, X)
  OPCODE_LDA_A = 0xAD,    // Absolute
  OPCODE_NOP = 0xEA,      // No operation
};

// Note: "int" causes overflow when profiling for any amount of time.
// typedef uint32_t Profile;
// i.e.
//  double nPercent = static_cast<double>(100 * tProfileOpcode.uProfile) /
//  nOpcodeTotal; // overflow
using Profile = double;

struct ProfileOpcode {
  int opcode;
  Profile count;  // Histogram

  // functor
  auto operator()(const ProfileOpcode& rLHS,
                  const ProfileOpcode& rRHS) const -> bool {
    return (rLHS.count > rRHS.count);
  }
};

struct ProfileOpmode {
  int opmode;
  Profile count;  // Histogram

  // functor
  auto operator()(const ProfileOpmode& rLHS,
                  const ProfileOpmode& rRHS) const -> bool {
    return rLHS.count > rRHS.count;
  }
};

enum ProfileFormat : uint8_t {
  PROFILE_FORMAT_SPACE,
  PROFILE_FORMAT_TAB,
  PROFILE_FORMAT_COMMA,
};

// Memory

const int DBG_6502_BRANCH_POS = +127;
const int DBG_6502_BRANCH_NEG = -128;
const uint32_t DBG_6502_ZEROPAGE_END = 0x00FF;
const uint32_t DBG_6502_STACK_BEGIN = 0x0100;
const uint32_t DBG_6502_STACK_END = 0x01FF;
const uint32_t DBG_6502_IO_BEGIN = 0xC000;
const uint32_t DBG_6502_IO_END = 0xC0FF;
const uint32_t DBG_6502_BRK_VECTOR = 0xFFFE;
const uint32_t DBG_6502_MEM_BEGIN = 0x0000;

enum Device : uint8_t {
  DEV_MEMORY,
  DEV_DISK2,
  DEV_SY6522,
  DEV_AY8910,
  NUM_DEVICES,
};

enum MemoryView : uint8_t {
  MEM_VIEW_HEX,

  // 0x00 .. 0x1F Ctrl              (Inverse)
  // 0x20 .. 0x7F Flash / MouseText (Cyan)
  // 0x80 .. 0x9F Hi-Bit Ctrl       (Yellow)
  // 0xA0 .. 0xFF Hi-Bit Normal     (White)
  MEM_VIEW_ASCII,
  MEM_VIEW_APPLE,  // Low-Bit ASCII (Colorized Background)
  NUM_MEM_VIEWS,
};

struct MemoryDump {
  bool bActive;
  uint16_t address;
  Device eDevice;
  MemoryView eView;
};

enum MemoryDumpId : uint8_t { MEM_DUMP_1, MEM_DUMP_2, NUM_MEM_DUMPS };

constexpr int NUM_MEM_MINI_DUMPS = 2;

enum MemorySearchId : uint32_t {
  MEM_SEARCH_BYTE_EXACT,      // xx
  MEM_SEARCH_NIB_LOW_EXACT,   // ?x
  MEM_SEARCH_NIB_HIGH_EXACT,  // x?
  MEM_SEARCH_BYTE_1_WILD,     // ?
  MEM_SEARCH_BYTE_N_WILD,     // ??

  MEM_SEARCH_TYPE_MASK = (1 << 16) - 1,
  MEM_SEARCH_FOUND = (1 << 16),
};

struct MemorySearch {
  uint8_t value;        // search value
  MemorySearchId type;  //
  bool found;           //
};

using MemorySearchValues = std::vector<MemorySearch>;
using MemorySearchResults = std::vector<int>;

// Parameters

/* i.e.
    SYM LOAD = $C600   (1) type: string, nVal1 = symlookup; (2) type: operator,
   token: EQUAL; (3) type: address, token:DOLLAR BP LOAD            type: BP
   $LOAD           type: (1) = symbol, val=1adress
*/
enum ArgToken : uint8_t {  // Arg Token Type
  // Single Char Tokens must come first
  TOKEN_ALPHANUMERIC,  //
  TOKEN_AMPERSAND,     // &
  TOKEN_AT,            // @  results dereference. i.e. S 0,FFFF C030; L @1
  TOKEN_BRACE_L,       // {
  TOKEN_BRACE_R,       // }
  TOKEN_BRACKET_L,     // [
  TOKEN_BRACKET_R,     // ]
  TOKEN_BSLASH,        // \xx Hex Literal
  TOKEN_CARET,         // ^
  TOKEN_COLON,         // : Range
  TOKEN_COMMA,         // , Length
  TOKEN_DOLLAR,        // $ Address (symbol lookup forced)
  TOKEN_EQUAL,         // = Assign Argment.n2 = Argument2
  TOKEN_EXCLAMATION,   // !
  TOKEN_FSLASH,        // /
  TOKEN_GREATER_THAN,  // >
  TOKEN_HASH,          // # Value  no symbol lookup
  TOKEN_LESS_THAN,     // <
  TOKEN_MINUS,         // - Delta  Argument1 -= Argument2
  TOKEN_PAREN_L,       // (
  TOKEN_PAREN_R,       // )
  TOKEN_PERCENT,       // %
  TOKEN_PIPE,          // |
  TOKEN_PLUS,          // + Delta  Argument1 += Argument2
  TOKEN_QUOTE_SINGLE,  // '
  TOKEN_QUOTE_DOUBLE,  // "
  TOKEN_SEMI,          // ; Command Seperator
  TOKEN_SPACE,         //   Token Delimiter
  TOKEN_STAR,          // *
  TOKEN_TILDE,         // ~

  // Multi char tokens come last
  TOKEN_COMMENT_EOL,  // //
  TOKEN_FLAG_MULTI = TOKEN_COMMENT_EOL,
  TOKEN_GREATER_EQUAL,  // >=
  TOKEN_LESS_EQUAL,     // <=
  TOKEN_NOT_EQUAL,      // !=
  NUM_TOKENS,           // signal none, or bad
  NO_TOKEN = NUM_TOKENS,
};

enum ArgType : uint16_t {
  TYPE_ADDRESS = (1 << 0),  // $#### or $symbolname
  TYPE_OPERATOR = (1 << 1),
  TYPE_QUOTED_1 = (1 << 2),
  TYPE_QUOTED_2 = (1 << 3),  // "..."
  TYPE_STRING = (1 << 4),    // LOAD
  TYPE_RANGE = (1 << 5),
  TYPE_LENGTH = (1 << 6),
  TYPE_VALUE = (1 << 7),
  TYPE_NO_REG = (1 << 8),  // Don't do register value -> Argument.nValue
  TYPE_NO_SYM = (1 << 9),  // Don't do symbol lookup  -> Argument.nValue
};

struct TokenTable {
  ArgToken eToken;
  ArgType eType;
  char sToken[4];
};

struct Arg {
  char sArg[MAX_ARG_LEN];  // Array chars comes first, for alignment
  int nArgLen;             // Needed for TextSearch "ABC\x00"
  uint16_t nValue;         // 2
  // Enums and Bools should come last for alignment
  ArgToken eToken;  // 1/2/4
  int bType;          // 1/2/4 // Flags of ArgType
  Device eDevice;   // 1/2/4
  bool bSymbol;       // 1
};

// NOTE: Order MUST match parameters[] !!!
enum Parameters : uint8_t {
  // Note: Order must match Breakpoint_Operator_t
  // Note: Order must match PARAM_BREAKPOINT_*
  // Note: Order must match breakpoint_symbols
  PARAM_BREAKPOINT_BEGIN,
  PARAM_BP_LESS_EQUAL = PARAM_BREAKPOINT_BEGIN,  // <=
  PARAM_BP_LESS_THAN,                            // <
  PARAM_BP_EQUAL,                                // =
  PARAM_BP_NOT_EQUAL,                            // !=
  PARAM_BP_NOT_EQUAL_1,                          // !
  PARAM_BP_GREATER_THAN,                         // >
  PARAM_BP_GREATER_EQUAL,                        // >=
  PARAM_BP_READ,                                 // R
  PARAM_BP_READ_ALIAS,                           // ? alias READ
  PARAM_BP_WRITE,                                // W
  PARAM_BP_WRITE_ALIAS,                          // @ alias write
  PARAM_BP_READ_WRITE,                           // * alias READ WRITE
  PARAM_BREAKPOINT_END,
  PARAM_BREAKPOINT_NUM = PARAM_BREAKPOINT_END - PARAM_BREAKPOINT_BEGIN,

  // Note: Order must match Breakpoint_Source_t
  PARAM_REGS_BEGIN = PARAM_BREAKPOINT_END,  // Daisy Chain
  // Regs
  PARAM_REG_A = PARAM_REGS_BEGIN,
  PARAM_REG_X,
  PARAM_REG_Y,
  PARAM_REG_PC,  // Program Counter
  PARAM_REG_SP,  // Stack Pointer
  // Flags
  PARAM_FLAGS,   // Processor Status
  PARAM_FLAG_C,  // Carry
  PARAM_FLAG_Z,  // Zero
  PARAM_FLAG_I,  // Interrupt
  PARAM_FLAG_D,  // Decimal
  PARAM_FLAG_B,  // Break
  PARAM_FLAG_R,  // Reserved
  PARAM_FLAG_V,  // Overflow
  PARAM_FLAG_N,  // Sign
  PARAM_REGS_END,
  PARAM_REGS_NUM = PARAM_REGS_END - PARAM_REGS_BEGIN,

  // Disasm
  PARAM_CONFIG_BEGIN = PARAM_REGS_END,  // Daisy Chain
  PARAM_CONFIG_BRANCH =
      PARAM_CONFIG_BEGIN,  // config_disasm_branch_type   [0|1|2]
  PARAM_CONFIG_CLICK,      // config_disasm_click        [0..7] // GH#462
  PARAM_CONFIG_COLON,      // config_disasm_address_colon [0|1]
  PARAM_CONFIG_OPCODE,     // config_disasm_opcodes_view  [0|1]
  PARAM_CONFIG_POINTER,    // config_info_target_pointer  [0|1]
  PARAM_CONFIG_SPACES,     // config_disasm_opcode_spaces [0|1]
  PARAM_CONFIG_TARGET,     // config_disasm_targets      [0|1|2]
  PARAM_CONFIG_END,
  PARAM_CONFIG_NUM = PARAM_CONFIG_END - PARAM_CONFIG_BEGIN,

  // Disk
  PARAM_DISK_BEGIN = PARAM_CONFIG_END,  // Daisy Chain
  PARAM_DISK_EJECT = PARAM_DISK_BEGIN,  // DISK 1 EJECT
  PARAM_DISK_INFO,                      // DISK 1 INFO
  PARAM_DISK_PROTECT,                   // DISK 1 PROTECT
  PARAM_DISK_READ,  // DISK 1 READ Track Sector NumSectors MemAddress
  PARAM_DISK_END,
  PARAM_DISK_NUM = PARAM_DISK_END - PARAM_DISK_BEGIN,
  PARAM_FONT_BEGIN = PARAM_DISK_END,  // Daisy Chain
  PARAM_FONT_MODE = PARAM_FONT_BEGIN,
  PARAM_FONT_END,
  PARAM_FONT_NUM = PARAM_FONT_END - PARAM_FONT_BEGIN,
  PARAM_GENERAL_BEGIN = PARAM_FONT_END,  // Daisy Chain
  PARAM_FIND = PARAM_GENERAL_BEGIN,
  PARAM_BRANCH,
  PARAM_CATEGORY,
  PARAM_CLEAR,
  PARAM_LOAD,
  PARAM_LIST,
  PARAM_OFF,
  PARAM_ON,
  PARAM_RESET,
  PARAM_SAVE,
  PARAM_START,
  PARAM_STOP,
  PARAM_GENERAL_END,
  PARAM_GENERAL_NUM = PARAM_GENERAL_END - PARAM_GENERAL_BEGIN,
  PARAM_HELPCATEGORIES_BEGIN = PARAM_GENERAL_END,  // Daisy Chain
  PARAM_WILDSTAR = PARAM_HELPCATEGORIES_BEGIN,
  PARAM_CAT_BOOKMARKS,
  PARAM_CAT_BREAKPOINTS,
  PARAM_CAT_CONFIG,
  PARAM_CAT_CPU,
  PARAM_CAT_FLAGS,
  PARAM_CAT_HELP,
  PARAM_CAT_KEYBOARD,
  PARAM_CAT_MEMORY,
  PARAM_CAT_OUTPUT,
  PARAM_CAT_OPERATORS,
  PARAM_CAT_RANGE,
  PARAM_CAT_SYMBOLS,
  PARAM_CAT_VIEW,
  PARAM_CAT_WATCHES,
  PARAM_CAT_WINDOW,
  PARAM_CAT_ZEROPAGE,
  PARAM_HELPCATEGORIES_END,
  PARAM_HELPCATEGORIES_NUM = PARAM_HELPCATEGORIES_END -
      PARAM_HELPCATEGORIES_BEGIN,
  PARAM_MEM_SEARCH_BEGIN = PARAM_HELPCATEGORIES_END,  // Daisy Chain
  PARAM_MEM_SEARCH_WILD = PARAM_MEM_SEARCH_BEGIN,
  PARAM_MEM_SEARCH_END,
  PARAM_MEM_SEARCH_NUM = PARAM_MEM_SEARCH_END - PARAM_MEM_SEARCH_BEGIN,
  PARAM_SOURCE_BEGIN = PARAM_MEM_SEARCH_END,  // Daisy Chain
  PARAM_SRC_MEMORY = PARAM_SOURCE_BEGIN,
  PARAM_SRC_MEMORY_ALIAS,  // alias MEM = MEMORY
  PARAM_SRC_SYMBOLS,
  PARAM_SRC_SYMBOLS_ALIAS,  // alias SYM = SYMBOLS
  PARAM_SRC_MERLIN,
  PARAM_SRC_ORCA,
  PARAM_SOURCE_END,
  PARAM_SOURCE_NUM = PARAM_SOURCE_END - PARAM_SOURCE_BEGIN,
  PARAM_PROFILE_BEGIN = PARAM_SOURCE_END,  // Daisy Chain
  PARAM_PROFILE_RESET = PARAM_PROFILE_BEGIN,
  PARAM_PROFILE_SAVE,
  PARAM_PROFILE_LIST,
  PARAM_PROFILE_ON,
  PARAM_PROFILE_OFF,
  PARAM_PROFILE_END,
  PARAM_PROFILE_NUM = PARAM_PROFILE_END - PARAM_PROFILE_BEGIN,
  PARAM_WINDOW_BEGIN = PARAM_PROFILE_END,  // Daisy Chain
  // These are the "full screen" "windows" / Panels / Tab sheets
  PARAM_CODE = PARAM_WINDOW_BEGIN,  // disasm
  PARAM_CODE_2,                     // disasm bot
  PARAM_CONSOLE,
  PARAM_DATA,    // data all
  PARAM_DATA_2,  // data bot
  PARAM_DISASM,
  PARAM_INFO,  // Togle INFO on/off
  PARAM_SOURCE,
  PARAM_SRC_ALIAS,  // alias SRC = SOURCE
  PARAM_SOURCE_2,   // source bot
  PARAM_SYMBOLS,
  PARAM_SYM_ALIAS,  // alias SYM = SYMBOLS
  PARAM_SYMBOL_2,   // symbols bot
  PARAM_WINDOW_END,
  PARAM_WINDOW_NUM = PARAM_WINDOW_END - PARAM_WINDOW_BEGIN,
  NUM_PARAMS = PARAM_WINDOW_END,  // Daisy Chain
};

// Source Level Debugging
constexpr int NO_SOURCE_LINE = -1;

using SourceAssembly =
    std::map<uint16_t, int>;  // Address -> Line #  &  FileName

// Symbols

// ****************************************
// WARNING: This is the simple enumeration.
// See: symbols[]
// ****************************************
enum SymbolTable_Index_e : uint8_t  // SymbolsId -> SymbolTable_Index_e
{
  SYMBOLS_MAIN,
  SYMBOLS_APPLESOFT,
  SYMBOLS_ASSEMBLY,
  SYMBOLS_USER_1,
  SYMBOLS_USER_2,
  SYMBOLS_SRC_1,
  SYMBOLS_SRC_2,
  SYMBOLS_DOS33,
  SYMBOLS_PRODOS,
  NUM_SYMBOL_TABLES,
};

// ****************************************
// WARNING: This is the bit-flags to select which table.
// See: CmdSymbolsListTable()
// ****************************************
enum SymbolTable_Masks_e : uint16_t  // SymbolTableId ->
{
  SYMBOL_TABLE_MAIN = (1 << 0),
  SYMBOL_TABLE_APPLESOFT = (1 << 1),
  SYMBOL_TABLE_ASSEMBLY = (1 << 2),
  SYMBOL_TABLE_USER_1 = (1 << 3),
  SYMBOL_TABLE_USER_2 = (1 << 4),
  SYMBOL_TABLE_SRC_1 = (1 << 5),
  SYMBOL_TABLE_SRC_2 = (1 << 6),
  SYMBOL_TABLE_DOS33 = (1 << 7),
  SYMBOL_TABLE_PRODOS = (1 << 8),
};

using SymbolTable = std::map<uint16_t, std::string>;

// Watches
constexpr int MAX_WATCHES = 16;

// Window
enum Window : uint8_t {
  WINDOW_CODE,
  WINDOW_DATA,
  WINDOW_CONSOLE,
  NUM_WINDOWS,  // Not implemented yet
  WINDOW_IO,    // soft switches   $addr  name   state
  WINDOW_SYMBOLS,
  WINDOW_ZEROPAGE,
  WINDOW_SOURCE,
};

struct WindowConfig {
  bool bSplit;
  Window eTop;
  Window eBot;
  int left, top, right, bottom;
};
class VideoScannerDisplayInfo {
 public:
  VideoScannerDisplayInfo() noexcept
      : isDecimal(false),
        isHorzReal(false),
        isAbsCycle(false),
        lastCumulativeCycles(0),
        cycleDelta(0) {}
  auto Reset(void) -> void {
    lastCumulativeCycles = cumulative_cycles;
    cycleDelta = 0;
  }

  bool isDecimal;
  bool isHorzReal;
  bool isAbsCycle;

  uint64_t lastCumulativeCycles;
  uint32_t cycleDelta;
};

// Zero Page
constexpr int MAX_ZEROPAGE_POINTERS = 8;

enum Match : uint8_t { MATCH_EXACT, MATCH_FUZZY };

enum InputCursor : uint8_t {
  CURSOR_INSERT,
  CURSOR_OVERSTRIKE,
  NUM_INPUT_CURSORS,
};

constexpr int NUM_PROFILE_LINES = NUM_OPCODES + NUM_OPMODES + 16;
