#ifndef DEBUGGER_BREAKPOINTS_H
#define DEBUGGER_BREAKPOINTS_H

#include "Debugger_Types.h"

// Breakpoints
	extern Breakpoint_t g_aBreakpoints[ MAX_BREAKPOINTS ];
	extern int          g_nBreakpoints;
	extern int          g_bDebugBreakpointHit;
	extern const char *g_aBreakpointSource[ NUM_BREAKPOINT_SOURCES ];
	extern const char *g_aBreakpointSymbols[ NUM_BREAKPOINT_OPERATORS ];

// Prototypes _______________________________________________________________

	int  CheckBreakpointsIO ();
	int  CheckBreakpointsReg();
	void ClearTempBreakpoints();

	Update_t CmdBreakpoint       (int nArgs);
	Update_t CmdBreakpointAddPC  (int nArgs);
	Update_t CmdBreakpointAddSmart(int nArgs);
	Update_t CmdBreakpointAddReg (int nArgs);
	Update_t CmdBreakpointAddIO  (int nArgs);
	Update_t CmdBreakpointAddMem (int nArgs, BreakpointSource_t bpSrc);
	Update_t CmdBreakpointClear  (int nArgs);
	Update_t CmdBreakpointDisable(int nArgs);
	Update_t CmdBreakpointEdit   (int nArgs);
	Update_t CmdBreakpointEnable (int nArgs);
	Update_t CmdBreakpointList   (int nArgs);
	Update_t CmdBreakpointLoad   (int nArgs);
	Update_t CmdBreakpointSave   (int nArgs);

	Update_t CmdWatch          (int nArgs);
	Update_t CmdWatchAdd       (int nArgs);
	Update_t CmdWatchClear     (int nArgs);
	Update_t CmdWatchDisable   (int nArgs);
	Update_t CmdWatchEnable    (int nArgs);
	Update_t CmdWatchList      (int nArgs);
	Update_t CmdWatchLoad      (int nArgs);
	Update_t CmdWatchSave      (int nArgs);

	bool _CmdBreakpointAddReg( Breakpoint_t *pBP, BreakpointSource_t iSrc, BreakpointOperator_t iCmp, unsigned short nAddress, int nLen, bool bIsTempBreakpoint );
	int  _CmdBreakpointAddCommonArg ( int iArg, int nArg, BreakpointSource_t iSrc, BreakpointOperator_t iCmp, bool bIsTempBreakpoint = false );

// BWZ (Breakpoint, Watch, ZeroPage) shared helpers
	void _BWZ_Clear( Breakpoint_t * aBreakWatchZero, int iSlot );
	void _BWZ_RemoveOne( Breakpoint_t *aBreakWatchZero, const int iSlot, int & nTotal );
	void _BWZ_RemoveAll( Breakpoint_t *aBreakWatchZero, const int nMax, int & nTotal );
	void _BWZ_ClearViaArgs( int nArgs, Breakpoint_t * aBreakWatchZero, const int nMax, int & nTotal );
	void _BWZ_EnableDisableViaArgs( int nArgs, Breakpoint_t * aBreakWatchZero, const int nMax, const bool bEnabled );
	void _BWZ_List( const Breakpoint_t * aBreakWatchZero, const int iBWZ );
	void _BWZ_ListAll( const Breakpoint_t * aBreakWatchZero, const int nMax );

#endif
