// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdWindowCycleNext(int nArgs) -> Update_t;
auto CmdWindowCyclePrev(int nArgs) -> Update_t;
auto CmdWindowShowCode(int nArgs) -> Update_t;
auto CmdWindowShowCode1(int nArgs) -> Update_t;
auto CmdWindowShowCode2(int nArgs) -> Update_t;
auto CmdWindowShowData(int nArgs) -> Update_t;
auto CmdWindowShowData1(int nArgs) -> Update_t;
auto CmdWindowShowData2(int nArgs) -> Update_t;
auto CmdWindowShowSource(int nArgs) -> Update_t;
auto CmdWindowShowSource1(int nArgs) -> Update_t;
auto CmdWindowShowSource2(int nArgs) -> Update_t;
auto CmdWindowViewCode(int nArgs) -> Update_t;
auto CmdWindowViewConsole(int nArgs) -> Update_t;
auto CmdWindowViewData(int nArgs) -> Update_t;
auto CmdWindowViewOutput(int nArgs) -> Update_t;
auto CmdWindowViewSource(int nArgs) -> Update_t;
auto CmdWindowViewSymbols(int nArgs) -> Update_t;
auto CmdWindow(int nArgs) -> Update_t;
auto CmdWindowLast(int nArgs) -> Update_t;

auto CmdCursorFollowTarget(int nArgs) -> Update_t;
auto CmdCursorLineDown(int nArgs) -> Update_t;
auto CmdCursorLineUp(int nArgs) -> Update_t;
auto CmdCursorJumpPC(int nArgs) -> Update_t;
auto CmdCursorJumpRetAddr(int nArgs) -> Update_t;
auto CmdCursorRunUntil(int nArgs) -> Update_t;
auto CmdCursorPageDown(int nArgs) -> Update_t;
auto CmdCursorPageDown256(int nArgs) -> Update_t;
auto CmdCursorPageDown4K(int nArgs) -> Update_t;
auto CmdCursorPageUp(int nArgs) -> Update_t;
auto CmdCursorPageUp256(int nArgs) -> Update_t;
auto CmdCursorPageUp4K(int nArgs) -> Update_t;
auto CmdCursorSetPC(int nArgs) -> Update_t;

auto CmdViewOutput_Text4X(int nArgs) -> Update_t;
auto CmdViewOutput_Text41(int nArgs) -> Update_t;
auto CmdViewOutput_Text42(int nArgs) -> Update_t;
auto CmdViewOutput_Text8X(int nArgs) -> Update_t;
auto CmdViewOutput_Text81(int nArgs) -> Update_t;
auto CmdViewOutput_Text82(int nArgs) -> Update_t;
auto CmdViewOutput_GRX(int nArgs) -> Update_t;
auto CmdViewOutput_GR1(int nArgs) -> Update_t;
auto CmdViewOutput_GR2(int nArgs) -> Update_t;
auto CmdViewOutput_DGRX(int nArgs) -> Update_t;
auto CmdViewOutput_DGR1(int nArgs) -> Update_t;
auto CmdViewOutput_DGR2(int nArgs) -> Update_t;
auto CmdViewOutput_HGRX(int nArgs) -> Update_t;
auto CmdViewOutput_HGR1(int nArgs) -> Update_t;
auto CmdViewOutput_HGR2(int nArgs) -> Update_t;
auto CmdViewOutput_DHGRX(int nArgs) -> Update_t;
auto CmdViewOutput_DHGR1(int nArgs) -> Update_t;
auto CmdViewOutput_DHGR2(int nArgs) -> Update_t;

auto WindowJoin() -> void;
auto WindowSplit(Window_e eNewBottomWindow) -> void;
auto WindowLast() -> void;
auto WindowSwitch(int eNewWindow) -> void;
auto WindowGetHeight(int iWindow) -> int;
auto WindowUpdateDisasmSize() -> void;
auto WindowUpdateConsoleDisplayedSize() -> void;
auto WindowUpdateSizes() -> void;
auto CmdWindowViewFull(int iNewWindow) -> Update_t;
auto CmdWindowViewCommon(int iNewWindow) -> Update_t;

enum ViewVideoPage_t {
  VIEW_PAGE_1 = (1 << 0),
  VIEW_PAGE_2 = (1 << 1),
  VIEW_PAGE_X = (1 << 2)  // XOR cycles Page 1 / Page 2
};

auto ViewOutput(ViewVideoPage_t iPage, int bVideoModeFlags) -> Update_t;

auto CursorMoveDownAligned(int nDelta) -> void;
auto CursorMoveUpAligned(int nDelta) -> void;

auto DisasmCalcTopFromCurAddress(bool bUpdateTop = true) -> void;
auto DisasmCalcCurFromTopAddress() -> void;
auto DisasmCalcBotFromTopAddress() -> void;
auto DisasmCalcTopBotAddress() -> void;
auto DisasmCalcAddressFromLines(uint16_t iAddress, int nLines) -> uint16_t;

auto debug_get_video_mode(uint32_t* pVideoMode) -> bool;
