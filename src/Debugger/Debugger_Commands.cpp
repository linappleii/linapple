// SPDX-License-Identifier: GPL-2.0-only
/*
linapple : An Apple //e emulator for Linux

Copyright (C) 2009-2014, Tom Charlesworth, Michael Pohoreski
Copyright (C) 2020, Thorsten Brehm

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Debugger commands
 *
 * Author: Copyright (C) 2011 - 2011 Michael Pohoreski
 */

#include "Debugger_Commands.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Console.h"
#include "Debugger_Memory.h"
#include "Debugger_Parser.h"
#include "Debugger_Types.h"

// Globals
std::vector<Command_t> g_sorted_commands;
int g_num_commands_with_aliases = 0;

// Implementation
auto ExecuteCommand(int nArgs) -> Update_t {
  Update_t bUpdateDisplay = UPDATE_NOTHING;

  if (nArgs > 0) {
    CmdFuncPtr_t pFunction = nullptr;
    int iCommandAlias = -1;
    int nFound = FindCommand(g_args[0].sArg, pFunction, &iCommandAlias);

    if (nFound == 1) {
      if (pFunction) {
        bUpdateDisplay |= pFunction(nArgs - 1);
      }
    } else if (nFound > 1) {
      DisplayAmbigiousCommands(nFound);
    } else {
      uint16_t address = 0;
      if (ArgsGetValue(&g_args[0], &address)) {
        g_disasm_cur_address = address;
        if (g_window_this == WINDOW_DATA) {
          g_mem_dump[0].address = address;
        }
        DisasmCalcTopBotAddress();
        bUpdateDisplay |= UPDATE_DISASM;
      } else {
        char sText[CONSOLE_WIDTH];
        snprintf(sText, sizeof(sText), "Unknown command: %s", g_args[0].sArg);
        bUpdateDisplay |= console_display_error(sText);
      }
    }
  }

  return bUpdateDisplay;
}

auto DebuggerProcessCommand(const bool bEchoConsoleInput) -> Update_t {
  Update_t bUpdateDisplay = UPDATE_NOTHING;

  char sText[CONSOLE_WIDTH];

  if (bEchoConsoleInput) {
    ConsoleDisplayPush(ConsoleInputPeek());
  }

  if (g_assembler_input) {
    if (g_console_input_chars) {
      ParseInput(g_console_input_ptr, false);  // Don't cook the args
      bUpdateDisplay |= CmdAssemble(g_assembler_address, 0, g_arg_raw_count);
    } else {
      AssemblerOff();

      int nDelayedTargets = AssemblerDelayedTargetsSize();
      if (nDelayedTargets) {
        snprintf(sText, sizeof(sText), " Asm: %d sym declared, not defined",
                 nDelayedTargets);
        ConsoleDisplayPush(sText);
        bUpdateDisplay |= UPDATE_CONSOLE_DISPLAY;
      }
    }
    ConsoleInputReset();
    bUpdateDisplay |= UPDATE_CONSOLE_DISPLAY | UPDATE_CONSOLE_INPUT;
    ConsoleUpdate();  // udpate console, don't pause
  } else if (g_console_input_chars) {
    int nArgs = ParseInput(g_console_input_ptr);
    if (nArgs == ARG_SYNTAX_ERROR) {
      snprintf(sText, sizeof(sText), "Syntax error: %s", g_args[0].sArg);
      bUpdateDisplay |= console_display_error(sText);
    } else if (nArgs > 0) {
      bUpdateDisplay |= ExecuteCommand(nArgs);
    }

    if (!g_console_buffer_paused) {
      ConsoleInputReset();
    }
  }

  return bUpdateDisplay;
}

//===========================================================================

#define DEBUGGER__COMMANDS_VERIFY_TXT__ "\xDE\xAD\xC0\xDE"

// Setting function to nullptr, allows g_commands arguments to be safely listed
// here Commands should be listed alphabetically per category. For the list
// sorted by category, check Commands_e NOTE: Keep in sync Commands_e and
// g_commands[] ! Aliases are listed at the end.
Command_t g_commands[] = {
    // Assembler
    //		{"!", CmdAssemberMini, CMD_ASSEMBLER_MINI, "Mini
    // assembler"},
    {"A", CmdAssemble, CMD_ASSEMBLE, "Assemble instructions"},
    // CPU (Main)
    {".", CmdCursorJumpPC, CMD_CURSOR_JUMP_PC,
     "Locate the cursor in the disasm window"},  // centered
    {"=", CmdCursorSetPC, CMD_CURSOR_SET_PC,
     "Sets the PC to the current instruction"},
    {"G", CmdGoNormalSpeed, CMD_GO_NORMAL_SPEED,
     "Run at normal speed [until PC == address]"},
    {"GG", CmdGoFullSpeed, CMD_GO_FULL_SPEED,
     "Run at full speed [until PC == address]"},
    {"IN", CmdIn, CMD_IN, "Input byte from IO $C0xx"},
    {"KEY", CmdKey, CMD_INPUT_KEY, "Feed key into emulator"},
    {"JSR", CmdJSR, CMD_JSR, "Call sub-routine"},
    {"NOP", CmdNOP, CMD_NOP, "Zap the current instruction with a NOP"},
    {"OUT", CmdOut, CMD_OUT, "Output byte to IO $C0xx"},
    // CPU - Meta info
    {"PROFILE", CmdProfile, CMD_PROFILE, "List/Save 6502 profiling"},
    {"R", CmdRegisterSet, CMD_REGISTER_SET, "Set register"},
    // CPU - Stack
    {"POP", CmdStackPop, CMD_STACK_POP, nullptr},
    {"PPOP", CmdStackPopPseudo, CMD_STACK_POP_PSEUDO, nullptr},
    {"PUSH", CmdStackPop, CMD_STACK_PUSH, nullptr},
    //		{"RTS", CmdStackReturn, CMD_STACK_RETURN,
    // nullptr},
    {"P", CmdStepOver, CMD_STEP_OVER, "Step current instruction"},
    {"RTS", CmdStepOut, CMD_STEP_OUT, "Step out of subroutine"},
    // CPU - Meta info
    {"T", CmdTrace, CMD_TRACE, "Trace current instruction"},
    {"TF", CmdTraceFile, CMD_TRACE_FILE,
     "Save trace to filename [with video scanner info]"},
    {"TL", CmdTraceLine, CMD_TRACE_LINE, "Trace (with cycle counting)"},
    {"U", CmdUnassemble, CMD_UNASSEMBLE, "Disassemble instructions"},
    //		{"WAIT"        , CmdWait              , CMD_WAIT
    //, "Run until
    // Bookmarks
    {"BM", CmdBookmark, CMD_BOOKMARK, "Alias for BMA (Bookmark_t Add)"},
    {"BMA", CmdBookmarkAdd, CMD_BOOKMARK_ADD, "Add/Update addess to bookmark"},
    {"BMC", CmdBookmarkClear, CMD_BOOKMARK_CLEAR, "Clear (remove) bookmark"},
    {"BML", CmdBookmarkList, CMD_BOOKMARK_LIST, "List all bookmarks"},
    {"BMG", CmdBookmarkGoto, CMD_BOOKMARK_GOTO, "Move cursor to bookmark"},
    //		{"BMLOAD", CmdBookmarkLoad, CMD_BOOKMARK_LOAD,
    //"Load bookmarks"},
    {"BMSAVE", CmdBookmarkSave, CMD_BOOKMARK_SAVE, "Save bookmarks"},
    // Breakpoints
    {"BRK", CmdBreakInvalid, CMD_BREAK_INVALID,
     "Enter debugger on BRK or INVALID"},
    {"BRKOP", CmdBreakOpcode, CMD_BREAK_OPCODE, "Enter debugger on opcode"},
    {"BP", CmdBreakpoint, CMD_BREAKPOINT,
     "Alias for BPR (Breakpoint Register Add)"},
    {"BPA", CmdBreakpointAddSmart, CMD_BREAKPOINT_ADD_SMART,
     "Add (smart) breakpoint"},
    //		{"BPP", CmdBreakpointAddFlag,
    // CMD_BREAKPOINT_ADD_FLAG, "Add breakpoint on flags"},
    {"BPR", CmdBreakpointAddReg, CMD_BREAKPOINT_ADD_REG,
     "Add breakpoint on register value"},  // NOTE! Different from
                                           // SoftICE !!!!
    {"BPX", CmdBreakpointAddPC, CMD_BREAKPOINT_ADD_PC,
     "Add breakpoint at current instruction"},
    {"BPIO", CmdBreakpointAddIO, CMD_BREAKPOINT_ADD_IO,
     "Add breakpoint for IO address $C0xx"},
    {"BPM", CmdBreakpointAddMemA, CMD_BREAKPOINT_ADD_MEM,
     "Add breakpoint on memory access"},  // SoftICE
    {"BPMR", CmdBreakpointAddMemR, CMD_BREAKPOINT_ADD_MEMR,
     "Add breakpoint on memory read access"},
    {"BPMW", CmdBreakpointAddMemW, CMD_BREAKPOINT_ADD_MEMW,
     "Add breakpoint on memory write access"},

    {"BPC", CmdBreakpointClear, CMD_BREAKPOINT_CLEAR,
     "Clear (remove) breakpoint"},  // SoftICE
    {"BPD", CmdBreakpointDisable, CMD_BREAKPOINT_DISABLE,
     "Disable breakpoint- it is still in the list, just not "
     "active"},  // SoftICE
    {"BPEDIT", CmdBreakpointEdit, CMD_BREAKPOINT_EDIT,
     "Edit breakpoint"},  // SoftICE
    {"BPE", CmdBreakpointEnable, CMD_BREAKPOINT_ENABLE,
     "(Re)Enable disabled breakpoint"},  // SoftICE
    {"BPL", CmdBreakpointList, CMD_BREAKPOINT_LIST,
     "List all breakpoints"},  // SoftICE
                               //		{"BPLOAD", CmdBreakpointLoad,
                               // CMD_BREAKPOINT_LOAD, "Loads breakpoints"},
    {"BPSAVE", CmdBreakpointSave, CMD_BREAKPOINT_SAVE, "Saves breakpoints"},
    // Config
    {"BENCHMARK", CmdBenchmark, CMD_BENCHMARK, "Benchmark the emulator"},
    {"BW", CmdConfigColorMono, CMD_CONFIG_BW,
     "Sets/Shows RGB for Black & White scheme"},
    {"COLOR", CmdConfigColorMono, CMD_CONFIG_COLOR,
     "Sets/Shows RGB for color scheme"},
    //		{"OPTION", CmdConfigMenu, CMD_CONFIG_MENU,
    //"Access config options"},
    {"DISASM", CmdConfigDisasm, CMD_CONFIG_DISASM,
     "Sets/Shows disassembly view options."},
    {"FONT", CmdConfigFont, CMD_CONFIG_FONT,
     "Shows current font or sets new one"},
    {"HCOLOR", CmdConfigHColor, CMD_CONFIG_HCOLOR,
     "Sets/Shows colors mapped to Apple HGR"},
    {"LOAD", CmdConfigLoad, CMD_CONFIG_LOAD, "Load debugger configuration"},
    {"MONO", CmdConfigColorMono, CMD_CONFIG_MONOCHROME,
     "Sets/Shows RGB for monochrome scheme"},
    {"SAVE", CmdConfigSave, CMD_CONFIG_SAVE, "Save debugger configuration"},
    {"PWD", CmdConfigGetDebugDir, CMD_CONFIG_GET_DEBUG_DIR,
     "Displays the current debugger directory. Used for scripts & "
     "mem load/save."},
    {"CD", CmdConfigSetDebugDir, CMD_CONFIG_SET_DEBUG_DIR,
     "Updates the current debugger directory."},
    // Cursor
    {"RET", CmdCursorJumpRetAddr, CMD_CURSOR_JUMP_RET_ADDR,
     "Sets the cursor to the sub-routine caller"},
    {"^", nullptr, CMD_CURSOR_LINE_UP, nullptr},  // \x2191 = Up Arrow (Unicode)
    {"Shift ^", nullptr, CMD_CURSOR_LINE_UP_1, nullptr},
    {"v", nullptr, CMD_CURSOR_LINE_DOWN,
     nullptr},  // \x2193 = Dn Arrow (Unicode)
    {"Shift v", nullptr, CMD_CURSOR_LINE_DOWN_1, nullptr},
    {"PAGEUP", CmdCursorPageUp, CMD_CURSOR_PAGE_UP, "Scroll up one screen"},
    {"PAGEUP256", CmdCursorPageUp256, CMD_CURSOR_PAGE_UP_256,
     "Scroll up 256 bytes"},  // Shift
    {"PAGEUP4K", CmdCursorPageUp4K, CMD_CURSOR_PAGE_UP_4K,
     "Scroll up 4096 bytes"},  // Ctrl
    {"PAGEDN", CmdCursorPageDown, CMD_CURSOR_PAGE_DOWN,
     "Scroll down one scren"},
    {"PAGEDOWN256", CmdCursorPageDown256, CMD_CURSOR_PAGE_DOWN_256,
     "Scroll down 256 bytes"},  // Shift
    {"PAGEDOWN4K", CmdCursorPageDown4K, CMD_CURSOR_PAGE_DOWN_4K,
     "Scroll down 4096 bytes"},  // Ctrl
                                 // Cycles info
    {"CYCLES", CmdCyclesInfo, CMD_CYCLES_INFO, "Cycles display configuration"},
    // Disassembler Data
    {"Z", CmdDisasmDataDefByte1, CMD_DISASM_DATA, "Treat byte [range] as data"},
    {"X", CmdDisasmDataDefCode, CMD_DISASM_CODE, "Treat byte [range] as code"},
    // TODO: Conflicts with monitor command #L -> 000DL
    {"B", CmdDisasmDataList, CMD_DISASM_LIST,
     "List all byte ranges treated as data"},
    // without symbol lookup
    {"DB", CmdDisasmDataDefByte1, CMD_DEFINE_DATA_BYTE1, "Define byte(s)"},
    {"DB2", CmdDisasmDataDefByte2, CMD_DEFINE_DATA_BYTE2,
     "Define byte array, display 2 bytes/line"},
    {"DB4", CmdDisasmDataDefByte4, CMD_DEFINE_DATA_BYTE4,
     "Define byte array, display 4 bytes/line"},
    {"DB8", CmdDisasmDataDefByte8, CMD_DEFINE_DATA_BYTE8,
     "Define byte array, display 8 bytes/line"},
    {"DW", CmdDisasmDataDefWord1, CMD_DEFINE_DATA_WORD1,
     "Define address array"},
    {"DW2", CmdDisasmDataDefWord2, CMD_DEFINE_DATA_WORD2,
     "Define address array, display 2 words/line"},
    {"DW4", CmdDisasmDataDefWord4, CMD_DEFINE_DATA_WORD4,
     "Define address array, display 4 words/line"},
    {"ASC", CmdDisasmDataDefString, CMD_DEFINE_DATA_STR,
     "Define text string"},  // 2.7.0.26 Changed: DS to ASC because
                             // DS is used as "Define Space"
                             // assembler directive
                             //		{"DF", CmdDisasmDataDefFloat,
                             // CMD_DEFINE_DATA_FLOAT, "Define AppleSoft
                             // (packed) Float"},
                             //		{"DFX", CmdDisasmDataDefFloatUnpack,
                             // CMD_DEFINE_DATA_FLOAT2, "Define AppleSoft
                             // (unpacked) Float"},
                             // with symbol lookup
                             //		{"DA<>", CmdDisasmDataDefAddress8HL,
                             // CMD_DEFINE_ADDR_8_HL, "Define split array of
                             // addresses, high byte section followed by low
                             // byte section"},
                             //		{"DA><", CmdDisasmDataDefAddress8LH,
                             // CMD_DEFINE_ADDR_8_LH, "Define split array of
                             // addresses, low byte section followed by high
                             // byte section"},
                             //		{"DA<", CmdDisasmDataDefAddress8H,
                             // CMD_DEFINE_ADDR_BYTE_H, "Define array of high
                             // byte addresses"},
                             //		{"DB>", CmdDisasmDataDefAddress8L,
                             // CMD_DEFINE_ADDR_BYTE_L, "Define array of low
                             // byte addresses"}
    {"DA", CmdDisasmDataDefAddress16, CMD_DEFINE_ADDR_WORD,
     "Define array of word addresses"},
    // TODO: Rename config cmd: DISASM
    //		{"UA", CmdDisasmDataSmart,
    // CMD_SMART_DISASSEMBLE, "Analyze opcodes to determine if code
    // or data"},
    // Disk
    {"DISK", CmdDisk, CMD_DISK, "Access Disk Drive Functions"},
    // Flags
    //		{"FC", CmdFlag, CMD_FLAG_CLEAR, "Clear specified
    // Flag"}, // NVRBDIZC see AW_CPU.cpp AF_*
    // TODO: Conflicts with monitor command #L -> 000CL
    {"CL", CmdFlag, CMD_FLAG_CLEAR,
     "Clear specified Flag"},  // NVRBDIZC see AW_CPU.cpp AF_*

    {"CLC", CmdFlagClear, CMD_FLAG_CLR_C, "Clear Flag Carry"},  // 0 // Legacy
    {"CLZ", CmdFlagClear, CMD_FLAG_CLR_Z, "Clear Flag Zero"},   // 1
    {"CLI", CmdFlagClear, CMD_FLAG_CLR_I,
     "Clear Flag Interrupts Disabled"},                                 // 2
    {"CLD", CmdFlagClear, CMD_FLAG_CLR_D, "Clear Flag Decimal (BCD)"},  // 3
    {"CLB", CmdFlagClear, CMD_FLAG_CLR_B, "CLear Flag Break"},  // 4 // Legacy
    {"CLR", CmdFlagClear, CMD_FLAG_CLR_R, "Clear Flag Reserved"},         // 5
    {"CLV", CmdFlagClear, CMD_FLAG_CLR_V, "Clear Flag Overflow"},         // 6
    {"CLN", CmdFlagClear, CMD_FLAG_CLR_N, "Clear Flag Negative (Sign)"},  // 7

    //		{"FS", CmdFlag, CMD_FLAG_SET, "Set specified
    // Flag"},
    {"SE", CmdFlag, CMD_FLAG_SET, "Set specified Flag"},

    {"SEC", CmdFlagSet, CMD_FLAG_SET_C, "Set Flag Carry"},                // 0
    {"SEZ", CmdFlagSet, CMD_FLAG_SET_Z, "Set Flag Zero"},                 // 1
    {"SEI", CmdFlagSet, CMD_FLAG_SET_I, "Set Flag Interrupts Disabled"},  // 2
    {"SED", CmdFlagSet, CMD_FLAG_SET_D, "Set Flag Decimal (BCD)"},        // 3
    {"SEB", CmdFlagSet, CMD_FLAG_SET_B, "Set Flag Break"},     // 4 // Legacy
    {"SER", CmdFlagSet, CMD_FLAG_SET_R, "Set Flag Reserved"},  // 5
    {"SEV", CmdFlagSet, CMD_FLAG_SET_V, "Set Flag Overflow"},  // 6
    {"SEN", CmdFlagSet, CMD_FLAG_SET_N, "Set Flag Negative"},  // 7
                                                               // Help
    {"?", CmdHelpList, CMD_HELP_LIST, "List all available commands"},
    {"HELP", CmdHelpSpecific, CMD_HELP_SPECIFIC, "Help on specific command"},
    {"VERSION", CmdVersion, CMD_VERSION,
     "Displays version of emulator/debugger"},
    {"MOTD", CmdMOTD, CMD_MOTD, nullptr},  // MOTD: Message Of The Day
                                           // Memory
    {"MC", CmdMemoryCompare, CMD_MEMORY_COMPARE, nullptr},

    {"MD1", CmdMemoryMiniDumpHex, CMD_MEM_MINI_DUMP_HEX_1,
     "Hex dump in the mini memory area 1"},
    {"MD2", CmdMemoryMiniDumpHex, CMD_MEM_MINI_DUMP_HEX_2,
     "Hex dump in the mini memory area 2"},

    {"MA1", CmdMemoryMiniDumpAscii, CMD_MEM_MINI_DUMP_ASCII_1,
     "ASCII text in mini memory area 1"},
    {"MA2", CmdMemoryMiniDumpAscii, CMD_MEM_MINI_DUMP_ASCII_2,
     "ASCII text in mini memory area 2"},
    {"MT1", CmdMemoryMiniDumpApple, CMD_MEM_MINI_DUMP_APPLE_1,
     "Apple Text in mini memory area 1"},
    {"MT2", CmdMemoryMiniDumpApple, CMD_MEM_MINI_DUMP_APPLE_2,
     "Apple Text in mini memory area 2"},
    //		{"ML1", CmdMemoryMiniDumpLow,
    // CMD_MEM_MINI_DUMP_TXT_LO_1, "Text (Ctrl) in mini memory dump
    // area 1"},
    //		{"ML2", CmdMemoryMiniDumpLow,
    // CMD_MEM_MINI_DUMP_TXT_LO_2, "Text (Ctrl) in mini memory dump
    // area 2"},
    //		{"MH1", CmdMemoryMiniDumpHigh,
    // CMD_MEM_MINI_DUMP_TXT_HI_1, "Text (High) in mini memory dump
    // area 1"},
    //		{"MH2", CmdMemoryMiniDumpHigh,
    // CMD_MEM_MINI_DUMP_TXT_HI_2, "Text (High) in mini memory dump
    // area 2"},

    {"ME", CmdMemoryEdit, CMD_MEMORY_EDIT,
     "Memory Editor - Not Implemented!"},  // TODO: like Copy ][+
                                           // Sector Edit
    {"MEB", CmdMemoryEnterByte, CMD_MEMORY_ENTER_BYTE, "Enter byte"},
    {"MEW", CmdMemoryEnterWord, CMD_MEMORY_ENTER_WORD, "Enter word"},
    {"BLOAD", CmdMemoryLoad, CMD_MEMORY_LOAD, "Load a region of memory"},
    {"M", CmdMemoryMove, CMD_MEMORY_MOVE, "Memory move"},
    {"BSAVE", CmdMemorySave, CMD_MEMORY_SAVE, "Save a region of memory"},
    {"S", CmdMemorySearch, CMD_MEMORY_SEARCH,
     "Search memory for text / hex values"},
    {"@", SearchMemoryDisplay, CMD_MEMORY_FIND_RESULTS,
     "Display search memory results"},
    //		{"SA", CmdMemorySearchAscii,
    // CMD_MEMORY_SEARCH_ASCII, "Search ASCII text"},
    //		{"ST", CmdMemorySearchApple,
    // CMD_MEMORY_SEARCH_APPLE, "Search Apple text (hi-bit)"},
    {"SH", CmdMemorySearchHex, CMD_MEMORY_SEARCH_HEX,
     "Search memory for hex values"},
    {"F", CmdMemoryFill, CMD_MEMORY_FILL, "Memory fill"},

    {"NTSC", CmdNTSC, CMD_NTSC, "Save/Load the NTSC palette"},
    {"TSAVE", CmdTextSave, CMD_TEXT_SAVE, "Save text screen"},
    // Output / Scripts
    {"CALC", CmdOutputCalc, CMD_OUTPUT_CALC, "Display mini calc result"},
    {"ECHO", CmdOutputEcho, CMD_OUTPUT_ECHO,
     "Echo string to console"},  // or toggle command echoing"
    {"PRINT", CmdOutputPrint, CMD_OUTPUT_PRINT,
     "Display string and/or hex values"},
    {"PRINTF", CmdOutputPrintf, CMD_OUTPUT_PRINTF, "Display formatted string"},
    {"RUN", CmdOutputRun, CMD_OUTPUT_RUN,
     "Run script file of debugger commands"},
    // Source Level Debugging
    {"SOURCE", CmdSource, CMD_SOURCE, "Starts/Stops source level debugging"},
    {"SYNC", CmdSync, CMD_SYNC, "Syncs the cursor to the source file"},
    // Symbols
    {"SYM", CmdSymbols, CMD_SYMBOLS_LOOKUP,
     "Lookup symbol or address, or define symbol"},

    {"SYMMAIN", CmdSymbolsCommand, CMD_SYMBOLS_ROM,
     "Main/ROM symbol table lookup/menu"},  // CLEAR,LOAD,SAVE
    {"SYMBASIC", CmdSymbolsCommand, CMD_SYMBOLS_APPLESOFT,
     "Applesoft symbol table lookup/menu"},  // CLEAR,LOAD,SAVE
    {"SYMASM", CmdSymbolsCommand, CMD_SYMBOLS_ASSEMBLY,
     "Assembly symbol table lookup/menu"},  // CLEAR,LOAD,SAVE
    {"SYMUSER", CmdSymbolsCommand, CMD_SYMBOLS_USER_1,
     "First user symbol table lookup/menu"},  // CLEAR,LOAD,SAVE
    {"SYMUSER2", CmdSymbolsCommand, CMD_SYMBOLS_USER_2,
     "Second User symbol table lookup/menu"},  // CLEAR,LOAD,SAVE
    {"SYMSRC", CmdSymbolsCommand, CMD_SYMBOLS_SRC_1,
     "First Source symbol table lookup/menu"},  // CLEAR,LOAD,SAVE
    {"SYMSRC2", CmdSymbolsCommand, CMD_SYMBOLS_SRC_2,
     "Second Source symbol table lookup/menu"},  // CLEAR,LOAD,SAVE
    {"SYMDOS33", CmdSymbolsCommand, CMD_SYMBOLS_DOS33,
     "DOS 3.3 symbol table lookup/menu"},  // CLEAR,LOAD,SAVE
    {"SYMPRODOS", CmdSymbolsCommand, CMD_SYMBOLS_PRODOS,
     "ProDOS symbol table lookup/menu"},  // CLEAR,LOAD,SAVE

    {"SYMINFO", CmdSymbolsInfo, CMD_SYMBOLS_INFO, "Display summary of symbols"},
    {"SYMLIST", CmdSymbolsList, CMD_SYMBOLS_LIST,
     "Lookup symbol in main/user/src tables"},  // 'symbolname',
                                                // can't use param
                                                // '*'
                                                // Video-scanner info
    {"VIDEOINFO", CmdVideoScannerInfo, CMD_VIDEO_SCANNER_INFO,
     "Video-scanner display configuration"},
    // View
    {"TEXT", CmdViewOutput_Text4X, CMD_VIEW_TEXT4X,
     "View Text screen (current page)"},
    {"TEXT1", CmdViewOutput_Text41, CMD_VIEW_TEXT41, "View Text screen Page 1"},
    {"TEXT2", CmdViewOutput_Text42, CMD_VIEW_TEXT42, "View Text screen Page 2"},
    {"TEXT80", CmdViewOutput_Text8X, CMD_VIEW_TEXT8X,
     "View 80-col Text screen (current page)"},
    {"TEXT81", CmdViewOutput_Text81, CMD_VIEW_TEXT81,
     "View 80-col Text screen Page 1"},
    {"TEXT82", CmdViewOutput_Text82, CMD_VIEW_TEXT82,
     "View 80-col Text screen Page 2"},
    {"GR", CmdViewOutput_GRX, CMD_VIEW_GRX,
     "View Lo-Res screen (current page)"},
    {"GR1", CmdViewOutput_GR1, CMD_VIEW_GR1, "View Lo-Res screen Page 1"},
    {"GR2", CmdViewOutput_GR2, CMD_VIEW_GR2, "View Lo-Res screen Page 2"},
    {"DGR", CmdViewOutput_DGRX, CMD_VIEW_DGRX,
     "View Double lo-res (current page)"},
    {"DGR1", CmdViewOutput_DGR1, CMD_VIEW_DGR1, "View Double lo-res Page 1"},
    {"DGR2", CmdViewOutput_DGR2, CMD_VIEW_DGR2, "View Double lo-res Page 2"},
    {"HGR", CmdViewOutput_HGRX, CMD_VIEW_HGRX, "View Hi-res (current page)"},
    {"HGR1", CmdViewOutput_HGR1, CMD_VIEW_HGR1, "View Hi-res Page 1"},
    {"HGR2", CmdViewOutput_HGR2, CMD_VIEW_HGR2, "View Hi-res Page 2"},
    {"DHGR", CmdViewOutput_DHGRX, CMD_VIEW_DHGRX,
     "View Double Hi-res (current page)"},
    {"DHGR1", CmdViewOutput_DHGR1, CMD_VIEW_DHGR1, "View Double Hi-res Page 1"},
    {"DHGR2", CmdViewOutput_DHGR2, CMD_VIEW_DHGR2, "View Double Hi-res Page 2"},
    // Watch
    {"W", CmdWatch, CMD_WATCH, "Alias for WA (Watch Add)"},
    {"WA", CmdWatchAdd, CMD_WATCH_ADD, "Add/Update address or symbol to watch"},
    {"WC", CmdWatchClear, CMD_WATCH_CLEAR, "Clear (remove) watch"},
    {"WD", CmdWatchDisable, CMD_WATCH_DISABLE,
     "Disable specific watch - it is still in the list, just not "
     "active"},
    {"WE", CmdWatchEnable, CMD_WATCH_ENABLE, "(Re)Enable disabled watch"},
    {"WL", CmdWatchList, CMD_WATCH_LIST, "List all watches"},
    //		{"WLOAD", CmdWatchLoad, CMD_WATCH_LOAD, "Load
    // Watches"}, // Cant use as param to W
    {"WSAVE", CmdWatchSave, CMD_WATCH_SAVE, "Save Watches"},  // due to symbol
                                                              // look-up Window
    {"WIN", CmdWindow, CMD_WINDOW, "Show specified debugger window"},
    // CODE 0, CODE 1, CODE 2 ... ???
    {"CODE", CmdWindowViewCode, CMD_WINDOW_CODE,
     "Switch to full code window"},  // Can't use WC = WatchClear
    {"CODE1", CmdWindowShowCode1, CMD_WINDOW_CODE_1,
     "Show code on top split window"},
    {"CODE2", CmdWindowShowCode2, CMD_WINDOW_CODE_2,
     "Show code on bottom split window"},
    {"CONSOLE", CmdWindowViewConsole, CMD_WINDOW_CONSOLE,
     "Switch to full console window"},
    {"DATA", CmdWindowViewData, CMD_WINDOW_DATA, "Switch to full data window"},
    {"DATA1", CmdWindowShowData1, CMD_WINDOW_DATA_1,
     "Show data on top split window"},
    {"DATA2", CmdWindowShowData2, CMD_WINDOW_DATA_2,
     "Show data on bottom split window"},
    {"SOURCE1", CmdWindowShowSource1, CMD_WINDOW_SOURCE_1,
     "Show source on top split screen"},
    {"SOURCE2", CmdWindowShowSource2, CMD_WINDOW_SOURCE_2,
     "Show source on bottom split screen"},

    {"\\", CmdWindowViewOutput, CMD_WINDOW_OUTPUT,
     "Display Apple output until key pressed"},
    //		{"INFO", CmdToggleInfoPanel, CMD_WINDOW_TOGGLE,
    // nullptr},
    //		{"WINSOURCE", CmdWindowShowSource,
    // CMD_WINDOW_SOURCE, nullptr},
    //		{"ZEROPAGE", CmdWindowShowZeropage,
    // CMD_WINDOW_ZEROPAGE, nullptr},
    // Zero Page
    {"ZP", CmdZeroPage, CMD_ZEROPAGE_POINTER, "Alias for ZPA (Zero Page Add)"},
    {"ZP0", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_0,
     "Set/Update/Remove ZP watch 0 "},
    {"ZP1", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_1,
     "Set/Update/Remove ZP watch 1"},
    {"ZP2", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_2,
     "Set/Update/Remove ZP watch 2"},
    {"ZP3", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_3,
     "Set/Update/Remove ZP watch 3"},
    {"ZP4", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_4,
     "Set/Update/Remove ZP watch 4"},
    {"ZP5", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_5,
     "Set/Update/Remove ZP watch 5 "},
    {"ZP6", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_6,
     "Set/Update/Remove ZP watch 6"},
    {"ZP7", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_7,
     "Set/Update/Remove ZP watch 7"},
    {"ZPA", CmdZeroPageAdd, CMD_ZEROPAGE_POINTER_ADD,
     "Add/Update address to zero page pointer"},
    {"ZPC", CmdZeroPageClear, CMD_ZEROPAGE_POINTER_CLEAR,
     "Clear (remove) zero page pointer"},
    {"ZPD", CmdZeroPageDisable, CMD_ZEROPAGE_POINTER_DISABLE,
     "Disable zero page pointer - it is still in the list, just "
     "not active"},
    {"ZPE", CmdZeroPageEnable, CMD_ZEROPAGE_POINTER_ENABLE,
     "(Re)Enable disabled zero page pointer"},
    {"ZPL", CmdZeroPageList, CMD_ZEROPAGE_POINTER_LIST,
     "List all zero page pointers"},
    //		{"ZPLOAD", CmdZeroPageLoad,
    // CMD_ZEROPAGE_POINTER_LOAD, "Load zero page pointers"}, // Cant
    // use as param to ZP
    {"ZPSAVE", CmdZeroPageSave, CMD_ZEROPAGE_POINTER_SAVE,
     "Save zero page pointers"},  // due to symbol look-up

    //	{"TIMEDEMO", CmdTimeDemo, CMD_TIMEDEMO, nullptr}, //
    // CmdBenchmarkStart(), CmdBenchmarkStop()
    //	{"WC", CmdShowCodeWindow, nullptr, nullptr}, // Can't
    // use since WatchClear
    //	{"WD", CmdShowDataWindow, nullptr, nullptr}, //

    // Internal Consistency Check
    {DEBUGGER__COMMANDS_VERIFY_TXT__, nullptr, NUM_COMMANDS, nullptr},

    // Aliasies - Can be in any order
    {"->", nullptr, CMD_CURSOR_JUMP_PC, nullptr},
    {"Ctrl ->", nullptr, CMD_CURSOR_SET_PC, nullptr},
    {"Shift ->", nullptr, CMD_CURSOR_JUMP_PC, nullptr},  // at top
    {"INPUT", CmdIn, CMD_IN, nullptr},
    // Data
    // Flags - Clear
    {"RC", CmdFlagClear, CMD_FLAG_CLR_C, "Clear Flag Carry"},  // 0 // Legacy
    {"RZ", CmdFlagClear, CMD_FLAG_CLR_Z, "Clear Flag Zero"},   // 1
    {"RI", CmdFlagClear, CMD_FLAG_CLR_I,
     "Clear Flag Interrupts Disabled"},                                // 2
    {"RD", CmdFlagClear, CMD_FLAG_CLR_D, "Clear Flag Decimal (BCD)"},  // 3
    {"RB", CmdFlagClear, CMD_FLAG_CLR_B, "CLear Flag Break"},     // 4 // Legacy
    {"RR", CmdFlagClear, CMD_FLAG_CLR_R, "Clear Flag Reserved"},  // 5
    {"RV", CmdFlagClear, CMD_FLAG_CLR_V, "Clear Flag Overflow"},  // 6
    {"RN", CmdFlagClear, CMD_FLAG_CLR_N,
     "Clear Flag Negative (Sign)"},                        // 7
                                                           // Flags - Set
    {"SC", CmdFlagSet, CMD_FLAG_SET_C, "Set Flag Carry"},  // 0
    {"SZ", CmdFlagSet, CMD_FLAG_SET_Z, "Set Flag Zero"},   // 1
    {"SI", CmdFlagSet, CMD_FLAG_SET_I, "Set Flag Interrupts Disabled"},  // 2
    {"SD", CmdFlagSet, CMD_FLAG_SET_D, "Set Flag Decimal (BCD)"},        // 3
    {"SB", CmdFlagSet, CMD_FLAG_SET_B, "CLear Flag Break"},   // 4 // Legacy
    {"SR", CmdFlagSet, CMD_FLAG_SET_R, "Set Flag Reserved"},  // 5
    {"SV", CmdFlagSet, CMD_FLAG_SET_V, "Set Flag Overflow"},  // 6
    {"SN", CmdFlagSet, CMD_FLAG_SET_N, "Set Flag Negative"},  // 7
                                                              // Memory
    {"D", CmdMemoryMiniDumpHex, CMD_MEM_MINI_DUMP_HEX_1,
     "Hex dump in the mini memory area 1"},  // FIXME: Must also
                                             // work in DATA screen
    {"M1", CmdMemoryMiniDumpHex, CMD_MEM_MINI_DUMP_HEX_1, nullptr},  // alias
    {"M2", CmdMemoryMiniDumpHex, CMD_MEM_MINI_DUMP_HEX_2, nullptr},  // alias

    {"ME8", CmdMemoryEnterByte, CMD_MEMORY_ENTER_BYTE,
     nullptr},  // changed from EB -- bugfix: EB:## ##
    {"ME16", CmdMemoryEnterWord, CMD_MEMORY_ENTER_WORD, nullptr},
    {"MM", CmdMemoryMove, CMD_MEMORY_MOVE, nullptr},
    {"MS", CmdMemorySearch, CMD_MEMORY_SEARCH, nullptr},  // CmdMemorySearch
    {"P0", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_0, nullptr},
    {"P1", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_1, nullptr},
    {"P2", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_2, nullptr},
    {"P3", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_3, nullptr},
    {"P4", CmdZeroPagePointer, CMD_ZEROPAGE_POINTER_4, nullptr},
    {"REGISTER", CmdRegisterSet, CMD_REGISTER_SET, nullptr},
    //		{"RET", CmdStackReturn, CMD_STACK_RETURN,
    // nullptr},
    {"TRACE", CmdTrace, CMD_TRACE, nullptr},

    //		{"SYMBOLS", CmdSymbols, CMD_SYMBOLS_LOOKUP,
    //"Return "},
    //		{"SYMBOLS1", CmdSymbolsInfo, CMD_SYMBOLS_1,
    // nullptr},
    //		{"SYMBOLS2", CmdSymbolsInfo, CMD_SYMBOLS_2,
    // nullptr},
    //		{"SYM0", CmdSymbolsInfo, CMD_SYMBOLS_ROM,
    // nullptr},
    //		{"SYM1", CmdSymbolsInfo, CMD_SYMBOLS_APPLESOFT,
    // nullptr},
    //		{"SYM2", CmdSymbolsInfo, CMD_SYMBOLS_ASSEMBLY,
    // nullptr},
    //		{"SYM3", CmdSymbolsInfo, CMD_SYMBOLS_USER_1,
    // nullptr},
    //		{"SYM4", CmdSymbolsInfo, CMD_SYMBOLS_USER_2,
    // nullptr},
    //		{"SYM5", CmdSymbolsInfo, CMD_SYMBOLS_SRC_1,
    // nullptr},
    //		{"SYM6", CmdSymbolsInfo, CMD_SYMBOLS_SRC_2,
    // nullptr},
    {"SYMDOS", CmdSymbolsCommand, CMD_SYMBOLS_DOS33, nullptr},
    {"SYMPRO", CmdSymbolsCommand, CMD_SYMBOLS_PRODOS, nullptr},

    {"TEXT40", CmdViewOutput_Text4X, CMD_VIEW_TEXT4X, nullptr},
    {"TEXT41", CmdViewOutput_Text41, CMD_VIEW_TEXT41, nullptr},
    {"TEXT42", CmdViewOutput_Text42, CMD_VIEW_TEXT42, nullptr},

    //		{"WATCH", CmdWatchAdd, CMD_WATCH_ADD, nullptr},
    {"WINDOW", CmdWindow, CMD_WINDOW, nullptr},
    //		{"W?", CmdWatchAdd, CMD_WATCH_ADD, nullptr},
    {"ZAP", CmdNOP, CMD_NOP, nullptr},

    // DEPRECATED  -- Probably should be removed in a future version
    {"BENCH", CmdBenchmarkStart, CMD_BENCHMARK, nullptr},
    {"EXITBENCH", nullptr, CMD_BENCHMARK,
     nullptr},  // 2.8.03 was incorrectly alias with 'E' Bug #246.
                // // CmdBenchmarkStop
    {"MDB", CmdMemoryMiniDumpHex, CMD_MEM_MINI_DUMP_HEX_1,
     nullptr},  // MemoryDumpByte  // Did anyone actually use this??
                //		{"MEMORY", CmdMemoryMiniDumpHex,
                // CMD_MEM_MINI_DUMP_HEX_1, nullptr}, // MemoryDumpByte  // Did
                // anyone actually use this??
};

// Parameters
// _____________________________________________________________________________________

#define DEBUGGER__PARAMS_VERIFY_TXT__ "\xDE\xAD\xDA\x1A"

// NOTE: Order MUST match Parameters_e[] !!!
Command_t g_parameters[] = {
    // Breakpoint
    {"<=", nullptr, PARAM_BP_LESS_EQUAL, nullptr},
    {"<", nullptr, PARAM_BP_LESS_THAN, nullptr},
    {"=", nullptr, PARAM_BP_EQUAL, nullptr},
    {"!=", nullptr, PARAM_BP_NOT_EQUAL, nullptr},
    {"!", nullptr, PARAM_BP_NOT_EQUAL_1, nullptr},
    {">", nullptr, PARAM_BP_GREATER_THAN, nullptr},
    {">=", nullptr, PARAM_BP_GREATER_EQUAL, nullptr},
    {"R", nullptr, PARAM_BP_READ, nullptr},
    {"?", nullptr, PARAM_BP_READ, nullptr},
    {"W", nullptr, PARAM_BP_WRITE, nullptr},
    {"@", nullptr, PARAM_BP_WRITE, nullptr},
    {"*", nullptr, PARAM_BP_READ_WRITE, nullptr},
    // Regs (for PUSH / POP)
    {"A", nullptr, PARAM_REG_A, nullptr},
    {"X", nullptr, PARAM_REG_X, nullptr},
    {"Y", nullptr, PARAM_REG_Y, nullptr},
    {"PC", nullptr, PARAM_REG_PC, nullptr},
    {"S", nullptr, PARAM_REG_SP, nullptr},
    // Flags
    {"P", nullptr, PARAM_FLAGS, nullptr},
    {"C", nullptr, PARAM_FLAG_C, nullptr},  // ---- ---1 Carry
    {"Z", nullptr, PARAM_FLAG_Z, nullptr},  // ---- --1- Zero
    {"I", nullptr, PARAM_FLAG_I, nullptr},  // ---- -1-- Interrupt
    {"D", nullptr, PARAM_FLAG_D, nullptr},  // ---- 1--- Decimal
    {"B", nullptr, PARAM_FLAG_B, nullptr},  // ---1 ---- Break
    {"R", nullptr, PARAM_FLAG_R, nullptr},  // --1- ---- Reserved
    {"V", nullptr, PARAM_FLAG_V, nullptr},  // -1-- ---- Overflow
    {"N", nullptr, PARAM_FLAG_N, nullptr},  // 1--- ---- Sign
                                            // Disasm
    {"BRANCH", nullptr, PARAM_CONFIG_BRANCH, nullptr},
    {"CLICK", nullptr, PARAM_CONFIG_CLICK, nullptr},  // GH#462
    {"COLON", nullptr, PARAM_CONFIG_COLON, nullptr},
    {"OPCODE", nullptr, PARAM_CONFIG_OPCODE, nullptr},
    {"POINTER", nullptr, PARAM_CONFIG_POINTER, nullptr},
    {"SPACES", nullptr, PARAM_CONFIG_SPACES, nullptr},
    {"TARGET", nullptr, PARAM_CONFIG_TARGET, nullptr},
    // Disk
    {"EJECT", nullptr, PARAM_DISK_EJECT, nullptr},
    {"INFO", nullptr, PARAM_DISK_INFO, nullptr},
    {"PROTECT", nullptr, PARAM_DISK_PROTECT, nullptr},
    {"READ", nullptr, PARAM_DISK_READ, nullptr},
    // Font (Config)
    {"MODE", nullptr, PARAM_FONT_MODE,
     nullptr},  // also INFO, CONSOLE, DISASM (from Window)
                // General
    {"FIND", nullptr, PARAM_FIND, nullptr},
    {"BRANCH", nullptr, PARAM_BRANCH, nullptr},
    {"CATEGORY", nullptr, PARAM_CATEGORY, nullptr},
    {"CLEAR", nullptr, PARAM_CLEAR, nullptr},
    {"LOAD", nullptr, PARAM_LOAD, nullptr},
    {"LIST", nullptr, PARAM_LIST, nullptr},
    {"OFF", nullptr, PARAM_OFF, nullptr},
    {"ON", nullptr, PARAM_ON, nullptr},
    {"RESET", nullptr, PARAM_RESET, nullptr},
    {"SAVE", nullptr, PARAM_SAVE, nullptr},
    {"START", nullptr, PARAM_START, nullptr},  // benchmark
    {"STOP", nullptr, PARAM_STOP, nullptr},    // benchmark
                                               // Help Categories
    {"*", nullptr, PARAM_WILDSTAR, nullptr},
    {"BOOKMARKS", nullptr, PARAM_CAT_BOOKMARKS, nullptr},
    {"BREAKPOINTS", nullptr, PARAM_CAT_BREAKPOINTS, nullptr},
    {"CONFIG", nullptr, PARAM_CAT_CONFIG, nullptr},
    {"CPU", nullptr, PARAM_CAT_CPU, nullptr},
    //		{"EXPRESSION" ,
    {"FLAGS", nullptr, PARAM_CAT_FLAGS, nullptr},
    {"HELP", nullptr, PARAM_CAT_HELP, nullptr},
    {"KEYBOARD", nullptr, PARAM_CAT_KEYBOARD, nullptr},
    {"MEMORY", nullptr, PARAM_CAT_MEMORY,
     nullptr},  // alias // SOURCE [SYMBOLS] [MEMORY] filename
    {"OUTPUT", nullptr, PARAM_CAT_OUTPUT, nullptr},
    {"OPERATORS", nullptr, PARAM_CAT_OPERATORS, nullptr},
    {"RANGE", nullptr, PARAM_CAT_RANGE, nullptr},
    //		{"REGISTERS", nullptr, PARAM_CAT_REGISTERS, nullptr},
    {"SYMBOLS", nullptr, PARAM_CAT_SYMBOLS, nullptr},
    {"VIEW", nullptr, PARAM_CAT_VIEW, nullptr},
    {"WATCHES", nullptr, PARAM_CAT_WATCHES, nullptr},
    {"WINDOW", nullptr, PARAM_CAT_WINDOW, nullptr},
    {"ZEROPAGE", nullptr, PARAM_CAT_ZEROPAGE, nullptr},
    // Memory
    {"?", nullptr, PARAM_MEM_SEARCH_WILD, nullptr},
    //		{"*", nullptr, PARAM_MEM_SEARCH_BYTE, nullptr},
    // Source level debugging
    {"MEM", nullptr, PARAM_SRC_MEMORY, nullptr},
    {"MEMORY", nullptr, PARAM_SRC_MEMORY, nullptr},
    {"SYM", nullptr, PARAM_SRC_SYMBOLS, nullptr},
    {"SYMBOLS", nullptr, PARAM_SRC_SYMBOLS, nullptr},
    {"MERLIN", nullptr, PARAM_SRC_MERLIN, nullptr},
    {"ORCA", nullptr, PARAM_SRC_ORCA, nullptr},
    // Profile
    {"RESET", nullptr, PARAM_PROFILE_RESET, nullptr},
    {"SAVE", nullptr, PARAM_PROFILE_SAVE, nullptr},
    {"LIST", nullptr, PARAM_PROFILE_LIST, nullptr},
    {"ON", nullptr, PARAM_PROFILE_ON, nullptr},
    {"OFF", nullptr, PARAM_PROFILE_OFF, nullptr},
    // View
    //		{"VIEW", nullptr, PARAM_SRC_???, nullptr},
    // Window                                                       Win   Cmd
    // WinEffects      CmdEffects
    {"CODE", nullptr, PARAM_CODE,
     nullptr},  //   x     x    code win only   switch to code window
                //		{"CODE1", nullptr, PARAM_CODE_1, nullptr}, //   -     x
                // code/data win
    {"CODE2", nullptr, PARAM_CODE_2, nullptr},  //   -     x    code/data win
    {"CONSOLE", nullptr, PARAM_CONSOLE,
     nullptr},  //   x     -                    switch to console window
    {"DATA", nullptr, PARAM_DATA,
     nullptr},  //   x     x    data win only   switch to data window
                //		{"DATA1", nullptr, PARAM_DATA_1, nullptr}, //   -     x
                // code/data win
    {"DATA2", nullptr, PARAM_DATA_2, nullptr},   //   -     x    code/data win
    {"DISASM", nullptr, PARAM_DISASM, nullptr},  //
    {"INFO", nullptr, PARAM_INFO,
     nullptr},  //   -     x    code/data       Toggles showing/hiding
                //   Regs/Stack/BP/Watches/ZP
    {"SOURCE", nullptr, PARAM_SOURCE,
     nullptr},  //   x     x                    switch to source window
    {"SRC", nullptr, PARAM_SOURCE, nullptr},  // alias
    //		{"SOURCE_1", nullptr, PARAM_SOURCE_1, nullptr}, //   -     x
    // code/data
    {"SOURCE2 ", nullptr, PARAM_SOURCE_2, nullptr},  //   -     x
    {"SYMBOLS", nullptr, PARAM_SYMBOLS,
     nullptr},  //   x     x    code/data win   switch to symbols window
    {"SYM", nullptr, PARAM_SYMBOLS,
     nullptr},  // alias   x                    SOURCE [SYM] [MEM] filename
                //		{"SYMBOL1", nullptr, PARAM_SYMBOL_1, nullptr}, //   -     x
                // code/data win
    {"SYMBOL2", nullptr, PARAM_SYMBOL_2,
     nullptr},  //   -     x    code/data win
                // Internal Consistency Check
    {DEBUGGER__PARAMS_VERIFY_TXT__, nullptr, NUM_PARAMS, nullptr},
};

//===========================================================================

void VerifyDebuggerCommandTable() {
  g_num_commands_with_aliases = sizeof(g_commands) / sizeof(Command_t);

  for (int iCmd = 0; iCmd < NUM_COMMANDS; iCmd++) {
    if (g_commands[iCmd].command_id != iCmd) {
      fprintf(stderr,
              "*** ERROR *** Enumerated Commands mis-matched at #%d: %s!", iCmd,
              g_commands[iCmd].name);
    }
  }

  if (strcmp(g_commands[NUM_COMMANDS].name, DEBUGGER__COMMANDS_VERIFY_TXT__)) {
    fprintf(stderr, "*** ERROR *** Total Commands mis-matched!");
  }

  if (strcmp(g_parameters[NUM_PARAMS].name, DEBUGGER__PARAMS_VERIFY_TXT__)) {
    fprintf(stderr, "*** ERROR *** Total Parameters mis-matched!");
  }
}
