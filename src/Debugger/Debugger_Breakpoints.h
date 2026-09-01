#pragma once

#include "Debugger_Types.h"

// Breakpoints
extern Breakpoint_t g_breakpoints[MAX_BREAKPOINTS];
extern int g_breakpoints_count;
extern int g_debug_breakpoint_hit;
extern const char* g_breakpoint_source[NUM_BREAKPOINT_SOURCES];
extern const char* g_breakpoint_symbols[NUM_BREAKPOINT_OPERATORS];

// Prototypes _______________________________________________________________

int CheckBreakpointsIO();
int CheckBreakpointsReg();
void ClearTempBreakpoints();

Update_t CmdBreakpoint(int nArgs);
Update_t CmdBreakpointAddPC(int nArgs);
Update_t CmdBreakpointAddSmart(int nArgs);
Update_t CmdBreakpointAddReg(int nArgs);
Update_t CmdBreakpointAddIO(int nArgs);
Update_t CmdBreakpointAddMem(int nArgs, BreakpointSource_t bpSrc);
Update_t CmdBreakpointClear(int nArgs);
Update_t CmdBreakpointDisable(int nArgs);
Update_t CmdBreakpointEdit(int nArgs);
Update_t CmdBreakpointEnable(int nArgs);
Update_t CmdBreakpointList(int nArgs);
Update_t CmdBreakpointLoad(int nArgs);
Update_t CmdBreakpointSave(int nArgs);

Update_t CmdWatch(int nArgs);
Update_t CmdWatchAdd(int nArgs);
Update_t CmdWatchClear(int nArgs);
Update_t CmdWatchDisable(int nArgs);
Update_t CmdWatchEnable(int nArgs);
Update_t CmdWatchList(int nArgs);
Update_t CmdWatchLoad(int nArgs);
Update_t CmdWatchSave(int nArgs);

bool _CmdBreakpointAddReg(Breakpoint_t* pBP, BreakpointSource_t iSrc,
                          BreakpointOperator_t iCmp, uint16_t address, int nLen,
                          bool bIsTempBreakpoint);
int _CmdBreakpointAddCommonArg(int iArg, int nArg, BreakpointSource_t iSrc,
                               BreakpointOperator_t iCmp,
                               bool bIsTempBreakpoint = false);

// BWZ (Breakpoint, Watch, ZeroPage) shared helpers
void bwz_Clear(Breakpoint_t* aBreakWatchZero, int iSlot);
void bwz_RemoveOne(Breakpoint_t* aBreakWatchZero, const int iSlot, int& total);
void bwz_RemoveAll(Breakpoint_t* aBreakWatchZero, const int nMax, int& total);
void bwz_ClearViaArgs(int nArgs, Breakpoint_t* aBreakWatchZero, const int nMax,
                      int& total);
void bwz_EnableDisableViaArgs(int nArgs, Breakpoint_t* aBreakWatchZero,
                              const int nMax, const bool bEnabled);
void bwz_List(const Breakpoint_t* aBreakWatchZero, const int iBWZ);
void bwz_ListAll(const Breakpoint_t* aBreakWatchZero, const int nMax);
