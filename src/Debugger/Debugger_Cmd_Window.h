// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "Debugger_Types.h"

auto CmdWindowCycleNext(int nArgs) -> UpdateResult;
auto CmdWindowCyclePrev(int nArgs) -> UpdateResult;
auto CmdWindowShowCode(int nArgs) -> UpdateResult;
auto CmdWindowShowCode1(int nArgs) -> UpdateResult;
auto CmdWindowShowCode2(int nArgs) -> UpdateResult;
auto CmdWindowShowData(int nArgs) -> UpdateResult;
auto CmdWindowShowData1(int nArgs) -> UpdateResult;
auto CmdWindowShowData2(int nArgs) -> UpdateResult;
auto CmdWindowShowSource(int nArgs) -> UpdateResult;
auto CmdWindowShowSource1(int nArgs) -> UpdateResult;
auto CmdWindowShowSource2(int nArgs) -> UpdateResult;
auto CmdWindowViewCode(int nArgs) -> UpdateResult;
auto CmdWindowViewConsole(int nArgs) -> UpdateResult;
auto CmdWindowViewData(int nArgs) -> UpdateResult;
auto CmdWindowViewOutput(int nArgs) -> UpdateResult;
auto CmdWindowViewSource(int nArgs) -> UpdateResult;
auto CmdWindowViewSymbols(int nArgs) -> UpdateResult;
auto CmdWindow(int nArgs) -> UpdateResult;
auto CmdWindowLast(int nArgs) -> UpdateResult;

auto CmdCursorFollowTarget(int nArgs) -> UpdateResult;
auto CmdCursorLineDown(int nArgs) -> UpdateResult;
auto CmdCursorLineUp(int nArgs) -> UpdateResult;
auto CmdCursorJumpPC(int nArgs) -> UpdateResult;
auto CmdCursorJumpRetAddr(int nArgs) -> UpdateResult;
auto CmdCursorRunUntil(int nArgs) -> UpdateResult;
auto CmdCursorPageDown(int nArgs) -> UpdateResult;
auto CmdCursorPageDown256(int nArgs) -> UpdateResult;
auto CmdCursorPageDown4K(int nArgs) -> UpdateResult;
auto CmdCursorPageUp(int nArgs) -> UpdateResult;
auto CmdCursorPageUp256(int nArgs) -> UpdateResult;
auto CmdCursorPageUp4K(int nArgs) -> UpdateResult;
auto CmdCursorSetPC(int nArgs) -> UpdateResult;

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

auto WindowJoin() -> void;
auto WindowSplit(Window eNewBottomWindow) -> void;
auto WindowLast() -> void;
auto WindowSwitch(int eNewWindow) -> void;
auto WindowGetHeight(int iWindow) -> int;
auto WindowUpdateDisasmSize() -> void;
auto WindowUpdateConsoleDisplayedSize() -> void;
auto WindowUpdateSizes() -> void;
auto CmdWindowViewFull(int iNewWindow) -> UpdateResult;
auto CmdWindowViewCommon(int iNewWindow) -> UpdateResult;

enum ViewVideoPage : uint8_t {
  VIEW_PAGE_1 = (1 << 0),
  VIEW_PAGE_2 = (1 << 1),
  VIEW_PAGE_X = (1 << 2),  // XOR cycles Page 1 / Page 2
};

auto ViewOutput(ViewVideoPage iPage, int bVideoModeFlags) -> UpdateResult;

auto CursorMoveDownAligned(int nDelta) -> void;
auto CursorMoveUpAligned(int nDelta) -> void;

auto DisasmCalcTopFromCurAddress(bool bUpdateTop = true) -> void;
auto DisasmCalcCurFromTopAddress() -> void;
auto DisasmCalcBotFromTopAddress() -> void;
auto DisasmCalcTopBotAddress() -> void;
auto DisasmCalcAddressFromLines(uint16_t iAddress, int nLines) -> uint16_t;

auto debug_get_video_mode(uint32_t* pVideoMode) -> bool;
