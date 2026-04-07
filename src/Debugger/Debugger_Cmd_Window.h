#pragma once

#include "Debugger_Types.h"

Update_t CmdWindowCycleNext(int nArgs);
Update_t CmdWindowCyclePrev(int nArgs);
Update_t CmdWindowShowCode(int nArgs);
Update_t CmdWindowShowCode1(int nArgs);
Update_t CmdWindowShowCode2(int nArgs);
Update_t CmdWindowShowData(int nArgs);
Update_t CmdWindowShowData1(int nArgs);
Update_t CmdWindowShowData2(int nArgs);
Update_t CmdWindowShowSource(int nArgs);
Update_t CmdWindowShowSource1(int nArgs);
Update_t CmdWindowShowSource2(int nArgs);
Update_t CmdWindowViewCode(int nArgs);
Update_t CmdWindowViewConsole(int nArgs);
Update_t CmdWindowViewData(int nArgs);
Update_t CmdWindowViewOutput(int nArgs);
Update_t CmdWindowViewSource(int nArgs);
Update_t CmdWindowViewSymbols(int nArgs);
Update_t CmdWindow(int nArgs);
Update_t CmdWindowLast(int nArgs);

Update_t CmdCursorFollowTarget(int nArgs);
Update_t CmdCursorLineDown(int nArgs);
Update_t CmdCursorLineUp(int nArgs);
Update_t CmdCursorJumpPC(int nArgs);
Update_t CmdCursorJumpRetAddr(int nArgs);
Update_t CmdCursorRunUntil(int nArgs);
Update_t CmdCursorPageDown(int nArgs);
Update_t CmdCursorPageDown256(int nArgs);
Update_t CmdCursorPageDown4K(int nArgs);
Update_t CmdCursorPageUp(int nArgs);
Update_t CmdCursorPageUp256(int nArgs);
Update_t CmdCursorPageUp4K(int nArgs);
Update_t CmdCursorSetPC(int nArgs);

Update_t CmdViewOutput_Text4X(int nArgs);
Update_t CmdViewOutput_Text41(int nArgs);
Update_t CmdViewOutput_Text42(int nArgs);
Update_t CmdViewOutput_Text8X(int nArgs);
Update_t CmdViewOutput_Text81(int nArgs);
Update_t CmdViewOutput_Text82(int nArgs);
Update_t CmdViewOutput_GRX(int nArgs);
Update_t CmdViewOutput_GR1(int nArgs);
Update_t CmdViewOutput_GR2(int nArgs);
Update_t CmdViewOutput_DGRX(int nArgs);
Update_t CmdViewOutput_DGR1(int nArgs);
Update_t CmdViewOutput_DGR2(int nArgs);
Update_t CmdViewOutput_HGRX(int nArgs);
Update_t CmdViewOutput_HGR1(int nArgs);
Update_t CmdViewOutput_HGR2(int nArgs);
Update_t CmdViewOutput_DHGRX(int nArgs);
Update_t CmdViewOutput_DHGR1(int nArgs);
Update_t CmdViewOutput_DHGR2(int nArgs);

void _WindowJoin();
void _WindowSplit(Window_e eNewBottomWindow);
void _WindowLast();
void _WindowSwitch(int eNewWindow);
int  WindowGetHeight(int iWindow);
void WindowUpdateDisasmSize();
void WindowUpdateConsoleDisplayedSize();
void WindowUpdateSizes();
Update_t _CmdWindowViewFull(int iNewWindow);
Update_t _CmdWindowViewCommon(int iNewWindow);

enum ViewVideoPage_t
{
  VIEW_PAGE_1 = (1 << 0),
  VIEW_PAGE_2 = (1 << 1),
  VIEW_PAGE_X = (1 << 2) // XOR cycles Page 1 / Page 2
};

Update_t _ViewOutput(ViewVideoPage_t iPage, int bVideoModeFlags);

void _CursorMoveDownAligned(int nDelta);
void _CursorMoveUpAligned(int nDelta);

void DisasmCalcTopFromCurAddress(bool bUpdateTop = true);
void DisasmCalcCurFromTopAddress();
void DisasmCalcBotFromTopAddress();
void DisasmCalcTopBotAddress();
unsigned short DisasmCalcAddressFromLines(unsigned short iAddress, int nLines);

bool DebugGetVideoMode(unsigned int* pVideoMode);
