// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

// Breakpoints
extern Breakpoint_t g_breakpoints[MAX_BREAKPOINTS];
extern int g_breakpoints_count;
extern int g_debug_breakpoint_hit;
extern const char* g_breakpoint_source[NUM_BREAKPOINT_SOURCES];
extern const char* g_breakpoint_symbols[NUM_BREAKPOINT_OPERATORS];

// Prototypes _______________________________________________________________

auto CheckBreakpointsIO() -> int;
auto CheckBreakpointsReg() -> int;
auto ClearTempBreakpoints() -> void;

auto CmdBreakpoint(int nArgs) -> Update_t;
auto CmdBreakpointAddPC(int nArgs) -> Update_t;
auto CmdBreakpointAddSmart(int nArgs) -> Update_t;
auto CmdBreakpointAddReg(int nArgs) -> Update_t;
auto CmdBreakpointAddIO(int nArgs) -> Update_t;
auto CmdBreakpointAddMem(int nArgs, BreakpointSource_t bpSrc) -> Update_t;
auto CmdBreakpointClear(int nArgs) -> Update_t;
auto CmdBreakpointDisable(int nArgs) -> Update_t;
auto CmdBreakpointEdit(int nArgs) -> Update_t;
auto CmdBreakpointEnable(int nArgs) -> Update_t;
auto CmdBreakpointList(int nArgs) -> Update_t;
auto CmdBreakpointLoad(int nArgs) -> Update_t;
auto CmdBreakpointSave(int nArgs) -> Update_t;

auto CmdWatch(int nArgs) -> Update_t;
auto CmdWatchAdd(int nArgs) -> Update_t;
auto CmdWatchClear(int nArgs) -> Update_t;
auto CmdWatchDisable(int nArgs) -> Update_t;
auto CmdWatchEnable(int nArgs) -> Update_t;
auto CmdWatchList(int nArgs) -> Update_t;
auto CmdWatchLoad(int nArgs) -> Update_t;
auto CmdWatchSave(int nArgs) -> Update_t;

auto CmdBreakpointAddReg(Breakpoint_t* pBP, BreakpointSource_t iSrc,
                         BreakpointOperator_t iCmp, uint16_t address, int nLen,
                         bool bIsTempBreakpoint) -> bool;
auto CmdBreakpointAddCommonArg(int iArg, int nArg, BreakpointSource_t iSrc,
                               BreakpointOperator_t iCmp,
                               bool bIsTempBreakpoint = false) -> int;

// BWZ (Breakpoint, Watch, ZeroPage) shared helpers
auto bwz_Clear(Breakpoint_t* aBreakWatchZero, int iSlot) -> void;
auto bwz_RemoveOne(Breakpoint_t* aBreakWatchZero, const int iSlot, int& total)
    -> void;
auto bwz_RemoveAll(Breakpoint_t* aBreakWatchZero, const int nMax, int& total)
    -> void;
auto bwz_ClearViaArgs(int nArgs, Breakpoint_t* aBreakWatchZero, const int nMax,
                      int& total) -> void;
auto bwz_EnableDisableViaArgs(int nArgs, Breakpoint_t* aBreakWatchZero,
                              const int nMax, const bool bEnabled) -> void;
auto bwz_List(const Breakpoint_t* aBreakWatchZero, const int iBWZ) -> void;
auto bwz_ListAll(const Breakpoint_t* aBreakWatchZero, const int nMax) -> void;
