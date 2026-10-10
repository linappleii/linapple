// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

// Breakpoints
extern Breakpoint breakpoints[MAX_BREAKPOINTS];
extern int breakpoints_count;
extern int debug_breakpoint_hit;
extern int debug_break_on_invalid;
extern const char* breakpoint_source[NUM_BREAKPOINT_SOURCES];
extern const char* breakpoint_symbols[NUM_BREAKPOINT_OPERATORS];

// Prototypes _______________________________________________________________

auto CheckBreakpointsIO() -> int;
auto CheckBreakpointsReg() -> int;
auto ClearTempBreakpoints() -> void;

auto CmdBreakpoint(int nArgs) -> UpdateResult;
auto CmdBreakpointAddPC(int nArgs) -> UpdateResult;
auto CmdBreakpointAddSmart(int nArgs) -> UpdateResult;
auto CmdBreakpointAddReg(int nArgs) -> UpdateResult;
auto CmdBreakpointAddIO(int nArgs) -> UpdateResult;
auto CmdBreakpointAddMem(int nArgs, BreakpointSource bpSrc) -> UpdateResult;
auto CmdBreakpointClear(int nArgs) -> UpdateResult;
auto CmdBreakpointDisable(int nArgs) -> UpdateResult;
auto CmdBreakpointEdit(int nArgs) -> UpdateResult;
auto CmdBreakpointEnable(int nArgs) -> UpdateResult;
auto CmdBreakpointList(int nArgs) -> UpdateResult;
auto CmdBreakpointLoad(int nArgs) -> UpdateResult;
auto CmdBreakpointSave(int nArgs) -> UpdateResult;

auto CmdWatch(int nArgs) -> UpdateResult;
auto CmdWatchAdd(int nArgs) -> UpdateResult;
auto CmdWatchClear(int nArgs) -> UpdateResult;
auto CmdWatchDisable(int nArgs) -> UpdateResult;
auto CmdWatchEnable(int nArgs) -> UpdateResult;
auto CmdWatchList(int nArgs) -> UpdateResult;
auto CmdWatchLoad(int nArgs) -> UpdateResult;
auto CmdWatchSave(int nArgs) -> UpdateResult;

auto CmdBreakpointAddReg(Breakpoint* pBP, BreakpointSource iSrc,
                         BreakpointOperator iCmp, uint16_t address, int nLen,
                         bool bIsTempBreakpoint) -> bool;
auto CmdBreakpointAddCommonArg(int iArg, int nArg, BreakpointSource iSrc,
                               BreakpointOperator iCmp,
                               bool bIsTempBreakpoint = false) -> int;

// BWZ (Breakpoint, Watch, ZeroPage) shared helpers
auto bwz_Clear(Breakpoint* aBreakWatchZero, int iSlot) -> void;
auto bwz_RemoveOne(Breakpoint* aBreakWatchZero, int iSlot, int& total)
    -> void;
auto bwz_RemoveAll(Breakpoint* aBreakWatchZero, int nMax, int& total) -> void;
auto bwz_ClearViaArgs(int nArgs, Breakpoint* aBreakWatchZero, int nMax,
                      int& total) -> void;
auto bwz_EnableDisableViaArgs(int nArgs, Breakpoint* aBreakWatchZero,
                              int nMax, bool bEnabled) -> void;
auto bwz_List(const Breakpoint* aBreakWatchZero, int iBWZ) -> void;
auto bwz_ListAll(const Breakpoint* aBreakWatchZero, int nMax) -> void;
