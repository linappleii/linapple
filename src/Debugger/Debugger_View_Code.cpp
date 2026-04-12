#include "core/Common.h"
#include "Debug.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "Debugger_Display.h"
#include "Debugger_Parser.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Symbols.h"
#include "Debugger_Assembler.h"
#include "Debugger_DisassemblerData.h"
#include "core/Util_Text.h"

#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

// Externs for globals in Debugger_Display.cpp
extern int g_iWindowThis;
extern int g_nDisasmWinHeight;
extern unsigned short g_nDisasmTopAddress;
extern unsigned short g_nDisasmCurAddress;
extern unsigned short g_nDisasmBotAddress;
extern bool g_bDisasmCurBad;
extern int g_nDisasmCurLine;
extern int g_nFontHeight;
extern MemoryTextFile_t g_AssemblerSourceBuffer;
extern bool g_bConfigDisasmAddressView;
extern bool g_bConfigDisasmAddressColon;
extern bool g_bConfigDisasmOpcodesView;
extern bool g_bConfigDisasmOpcodeSpaces;
extern int g_iConfigDisasmBranchType;
extern int g_iConfigDisasmTargets;
extern bool g_bConfigInfoTargetPointer;

// Constants from Debugger_Display.cpp
const int DISPLAY_DISASM_RIGHT = 353;
const int DISPLAY_WIDTH = 560;
const int DISPLAY_FLAG_COLUMN = 357; // SCREENSPLIT1
const int DISPLAY_STACK_COLUMN = 357; // SCREENSPLIT1
const int MAX_DISPLAY_STACK_LINES = 8;

// Function prototypes for helpers in Debugger_Display.cpp
extern char ColorizeSpecialChar(char * sText, unsigned char nData, const MemoryView_e iView,
    const int iAsciBackground, const int iTextForeground,
    const int iHighBackground, const int iHighForeground,
    const int iCtrlBackground, const int iCtrlForeground);

extern void SetupColorsHiLoBits(bool bHighBit, bool bCtrlBit,
    int iTextBG, int iTextFG, int iHighBG, int iHighFG, int iCtrlBG, int iCtrlFG);

extern void DrawWindowBottom(Update_t bUpdate, int iWindow);
extern void DrawSubWindow_Info(Update_t bUpdate, int iWindow);
extern void DrawSubWindow_Source(Update_t bUpdate);

// --- Functions moved from Debugger_Display.cpp ---

unsigned short DrawDisassemblyLine(int iLine, const unsigned short nBaseAddress)
{
  if ((g_iWindowThis != WINDOW_CODE) && !((g_iWindowThis == WINDOW_DATA)))
    return 0;

  int iOpmode;
  int nOpbyte;
  DisasmLine_t line;
  const char* pSymbol = FindSymbolFromAddress(nBaseAddress);
  const char* pMnemonic = NULL;

  int bDisasmFormatFlags = GetDisassemblyLine(nBaseAddress, line);
  const DisasmData_t *pData = line.pDisasmData;

  iOpmode = line.iOpmode;
  nOpbyte = line.nOpbyte;

  const int nDefaultFontWidth = 7;

  enum TabStop_e
  {
      TS_OPCODE
    , TS_LABEL
    , TS_INSTRUCTION
    , TS_IMMEDIATE
    , TS_BRANCH
    , TS_CHAR
    , _NUM_TAB_STOPS
  };

  float aTabs[_NUM_TAB_STOPS] =
#if USE_APPLE_FONT
  { 5, 14, 26, 41, 48, 49 };
#else
  { 5.75, 15.5, 25, 40.5, 45.5, 48.5 };
#endif

#if !USE_APPLE_FONT
  if (!g_bConfigDisasmAddressColon)
    aTabs[TS_OPCODE] -= 1;

  if ((g_bConfigDisasmOpcodesView) && (!g_bConfigDisasmOpcodeSpaces))
  {
    aTabs[TS_LABEL] -= 3;
    aTabs[TS_INSTRUCTION] -= 2;
    aTabs[TS_IMMEDIATE] -= 1;
  }
#endif

  int iTab = 0;
  int nSpacer = 11;
  for (iTab = 0; iTab < _NUM_TAB_STOPS; iTab++)
  {
    if (!g_bConfigDisasmAddressView)
    {
      if (iTab < TS_IMMEDIATE)
        aTabs[iTab] -= 4;
    }
    if (!g_bConfigDisasmOpcodesView)
    {
      if (iTab < TS_IMMEDIATE)
      {
        aTabs[iTab] -= nSpacer;
        if (nSpacer > 0)
          nSpacer -= 2;
      }
    }
    aTabs[iTab] *= nDefaultFontWidth;
  }

  int nFontHeight = g_aFontConfig[FONT_DISASM_DEFAULT]._nLineHeight;

  RECT linerect;
  linerect.left = 0;
  linerect.top = iLine * nFontHeight;
  linerect.right = DISPLAY_DISASM_RIGHT;
  linerect.bottom = linerect.top + nFontHeight;

  bool bBreakpointActive;
  bool bBreakpointEnable;
  GetBreakpointInfo(nBaseAddress, bBreakpointActive, bBreakpointEnable);
  bool bAddressAtPC = (nBaseAddress == regs.pc);
  bool bAddressIsBookmark = Bookmark_Find(nBaseAddress);

  DebugColors_e iBackground = BG_DISASM_1;
  DebugColors_e iForeground = FG_DISASM_MNEMONIC;
  bool bCursorLine = false;

  if (((!g_bDisasmCurBad) && (iLine == g_nDisasmCurLine))
    || (g_bDisasmCurBad && (iLine == 0)))
  {
    bCursorLine = true;

    if (bBreakpointActive)
    {
      if (bBreakpointEnable)
      {
        iBackground = BG_DISASM_BP_S_C; iForeground = FG_DISASM_BP_S_C;
      }
      else
      {
        iBackground = BG_DISASM_BP_0_C; iForeground = FG_DISASM_BP_0_C;
      }
    }
    else
    if (bAddressAtPC)
    {
      iBackground = BG_DISASM_PC_C; iForeground = FG_DISASM_PC_C;
    }
    else
    {
      iBackground = BG_DISASM_C; iForeground = FG_DISASM_C;
      g_nDisasmCurAddress = nBaseAddress;
    }
  }
  else
  {
    if (iLine & 1) iBackground = BG_DISASM_1;
    else iBackground = BG_DISASM_2;

    if (bBreakpointActive)
    {
      if (bBreakpointEnable) iForeground = FG_DISASM_BP_S_X;
      else iForeground = FG_DISASM_BP_0_X;
    }
    else if (bAddressAtPC)
    {
      iBackground = BG_DISASM_PC_X; iForeground = FG_DISASM_PC_X;
    }
    else iForeground = FG_DISASM_MNEMONIC;
  }

  if (bAddressIsBookmark)
  {
    DebuggerSetColorBG(DebuggerGetColor(BG_DISASM_BOOKMARK));
    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_BOOKMARK));
  }
  else
  {
    DebuggerSetColorBG(DebuggerGetColor(iBackground));
    DebuggerSetColorFG(DebuggerGetColor(iForeground));
  }

  if (!bCursorLine)
    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));

  if (g_bConfigDisasmAddressView)
    PrintTextCursorX((const char*)line.sAddress, linerect);

  if (bAddressIsBookmark)
  {
    DebuggerSetColorBG(DebuggerGetColor(iBackground));
    DebuggerSetColorFG(DebuggerGetColor(iForeground));
  }

  if (!bCursorLine)
    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));

  if (g_bConfigDisasmAddressColon)
    PrintTextCursorX(":", linerect);
  else
    PrintTextCursorX(" ", linerect);

  linerect.left = (int)aTabs[TS_OPCODE];
  if (!bCursorLine)
    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPCODE));
  if (g_bConfigDisasmOpcodesView)
    PrintTextCursorX((const char*)line.sOpCodes, linerect);

  linerect.left = (int)aTabs[TS_LABEL];
  if (pSymbol)
  {
    if (!bCursorLine)
      DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_SYMBOL));
    PrintTextCursorX(pSymbol, linerect);
  }

  linerect.left = (int)aTabs[TS_INSTRUCTION];
  if (!bCursorLine)
  {
    if (pData) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_DIRECTIVE));
    else DebuggerSetColorFG(DebuggerGetColor(iForeground));
  }

  pMnemonic = line.sMnemonic;
  PrintTextCursorX(pMnemonic, linerect);
  PrintTextCursorX(" ", linerect);

  if (line.bTargetImmediate)
  {
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    PrintTextCursorX("#$", linerect);
  }

  if (line.bTargetIndexed || line.bTargetIndirect)
  {
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    PrintTextCursorX("(", linerect);
  }

  char *pTarget = line.sTarget;
  int nLen = strlen(pTarget);

  if (*pTarget == '$')
  {
    pTarget++;
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    PrintTextCursorX("$", linerect);
  }

  if (!bCursorLine)
  {
    if (bDisasmFormatFlags & DISASM_FORMAT_SYMBOL)
      DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_SYMBOL));
    else
    {
      if (iOpmode == AM_M) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPCODE));
      else DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_TARGET));
    }
  }

  int nMaxLen = MAX_TARGET_LEN;
  if (!g_bConfigDisasmAddressView) nMaxLen += 4;
  if (!g_bConfigDisasmOpcodesView) nMaxLen += (MAX_OPCODES * 3);

  int nOverflow = 0;
  if (bDisasmFormatFlags & DISASM_FORMAT_OFFSET)
  {
    if (line.nTargetOffset != 0) nOverflow++;
    nOverflow += strlen(line.sTargetOffset);
  }

  if (line.bTargetIndirect || line.bTargetX || line.bTargetY)
  {
    if (line.bTargetX) nOverflow += 2;
    else if ((line.bTargetY) && (!line.bTargetIndirect)) nOverflow += 2;
  }

  if (line.bTargetIndexed || line.bTargetIndirect) nOverflow++;
  if (line.bTargetIndexed)
  {
    if (line.bTargetY) nOverflow += 2;
  }

  if (bDisasmFormatFlags & DISASM_FORMAT_TARGET_POINTER)
  {
    nOverflow += strlen(line.sTargetPointer);
    nOverflow++; nOverflow += 2; nOverflow++;
  }

  if (bDisasmFormatFlags & DISASM_FORMAT_CHAR)
    nOverflow += strlen(line.sImmediate);

  if (nLen >= (nMaxLen - nOverflow))
    pTarget[nMaxLen - nOverflow] = 0;

  PrintTextCursorX(pTarget, linerect);

  if (bDisasmFormatFlags & DISASM_FORMAT_OFFSET)
  {
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    if (line.nTargetOffset > 0) PrintTextCursorX("+", linerect);
    else if (line.nTargetOffset < 0) PrintTextCursorX("-", linerect);
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPCODE));
    PrintTextCursorX(line.sTargetOffset, linerect);
  }

  if (line.bTargetIndirect || line.bTargetX || line.bTargetY)
  {
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    if (line.bTargetX)
    {
      PrintTextCursorX(",", linerect);
      if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_INFO_REG));
      PrintTextCursorX("X", linerect);
    }
    else if ((line.bTargetY) && (!line.bTargetIndirect))
    {
      PrintTextCursorX(",", linerect);
      if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_INFO_REG));
      PrintTextCursorX("Y", linerect);
    }
  }

  if (line.bTargetIndexed || line.bTargetIndirect)
  {
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    PrintTextCursorX(")", linerect);
  }

  if (line.bTargetIndexed)
  {
    if (line.bTargetY)
    {
      PrintTextCursorX(",", linerect);
      if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_INFO_REG));
      PrintTextCursorX("Y", linerect);
    }
  }

  if (pData) return nOpbyte;

  if (bDisasmFormatFlags & DISASM_FORMAT_TARGET_POINTER)
  {
    linerect.left = (int)aTabs[TS_IMMEDIATE];
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));
    PrintTextCursorX(line.sTargetPointer, linerect);
    if (bDisasmFormatFlags & DISASM_FORMAT_TARGET_VALUE)
    {
      if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
      if (g_iConfigDisasmTargets & DISASM_TARGET_BOTH) PrintTextCursorX(":", linerect);
      if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPCODE));
      PrintTextCursorX(line.sTargetValue, linerect);
      PrintTextCursorX(" ", linerect);
    }
  }

  if (bDisasmFormatFlags & DISASM_FORMAT_CHAR)
  {
    linerect.left = (int)aTabs[TS_CHAR];
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    if (!bCursorLine) ColorizeSpecialChar(NULL, line.nImmediate, MEM_VIEW_ASCII, iBackground);
    PrintTextCursorX(line.sImmediate, linerect);
    DebuggerSetColorBG(DebuggerGetColor(iBackground));
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
  }

  if (bDisasmFormatFlags & DISASM_FORMAT_BRANCH)
  {
    linerect.left = (int)aTabs[TS_BRANCH];
    if (!bCursorLine) DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_BRANCH));
#if !USE_APPLE_FONT
    if (g_iConfigDisasmBranchType == DISASM_BRANCH_FANCY)
      SelectObject(GetDebuggerMemDC(), g_aFontConfig[FONT_DISASM_BRANCH]._hFont);
#endif
    PrintText(line.sBranch, linerect);
#if !USE_APPLE_FONT
    if (g_iConfigDisasmBranchType)
      SelectObject(GetDebuggerMemDC(), g_aFontConfig[FONT_DISASM_DEFAULT]._hFont);
#endif
  }

  return nOpbyte;
}

void DrawFlags(int line, unsigned short nRegFlags, char* pFlagNames_)
{
  if ((g_iWindowThis != WINDOW_CODE) && !((g_iWindowThis == WINDOW_DATA)))
    return;

  char sFlagNames[_6502_NUM_FLAGS + 1] = "";
  char sText[8] = "?";
  RECT rect;

  int nFontWidth = g_aFontConfig[FONT_INFO]._nFontWidthAvg;
  int nSpacerWidth = nFontWidth;

  rect.top = line * g_nFontHeight;
  rect.bottom = rect.top + g_nFontHeight;
  rect.left = DISPLAY_FLAG_COLUMN;
  rect.right = rect.left + (10 * nFontWidth);

  DebuggerSetColorBG(DebuggerGetColor(BG_DATA_1));
  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_REG));
  PrintText("P ", rect);

  rect.top += g_nFontHeight;
  rect.bottom += g_nFontHeight;

  snprintf(sText, sizeof(sText), "%02X", nRegFlags);

  DebuggerSetColorBG(DebuggerGetColor(BG_INFO));
  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
  PrintText(sText, rect);

  rect.top -= g_nFontHeight;
  rect.bottom -= g_nFontHeight;
  sText[1] = 0;

  rect.left += ((2 + _6502_NUM_FLAGS) * nSpacerWidth);
  rect.right = rect.left + nFontWidth;

  int iFlag = 0;
  int nFlag = _6502_NUM_FLAGS;
  while (nFlag--)
  {
    iFlag = (_6502_NUM_FLAGS - nFlag - 1);
    bool bSet = (nRegFlags & 1);
    sText[0] = g_aBreakpointSource[BP_SRC_FLAG_C + iFlag][0];

    if (bSet)
    {
      DebuggerSetColorBG(DebuggerGetColor(BG_INFO_INVERSE));
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_INVERSE));
    }
    else
    {
      DebuggerSetColorBG(DebuggerGetColor(BG_INFO));
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
    }

    rect.left -= nSpacerWidth;
    rect.right -= nSpacerWidth;
    PrintText(sText, rect);

    rect.top += g_nFontHeight;
    rect.bottom += g_nFontHeight;
    DebuggerSetColorBG(DebuggerGetColor(BG_INFO));
    DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));

    sText[0] = '0' + static_cast<int>(bSet);
    PrintText(sText, rect);
    rect.top -= g_nFontHeight;
    rect.bottom -= g_nFontHeight;

    if (pFlagNames_)
    {
      if (!bSet) sFlagNames[nFlag] = '.';
      else sFlagNames[nFlag] = g_aBreakpointSource[BP_SRC_FLAG_C + iFlag][0];
    }
    nRegFlags >>= 1;
  }

  if (pFlagNames_) strcpy(pFlagNames_, sFlagNames);
}

void DrawStack(int line)
{
  if ((g_iWindowThis != WINDOW_CODE) && !((g_iWindowThis == WINDOW_DATA)))
    return;

  unsigned nAddress = regs.sp;
  int nFontWidth = g_aFontConfig[FONT_INFO]._nFontWidthAvg;
  DebuggerSetColorBG(DebuggerGetColor(BG_DATA_1));

  int iStack = 0;
  while (iStack < MAX_DISPLAY_STACK_LINES)
  {
    nAddress++;
    RECT rect;
    rect.left = DISPLAY_STACK_COLUMN;
    rect.top = (iStack + line) * g_nFontHeight;
    rect.right = rect.left + (10 * nFontWidth) + 1;
    rect.bottom = rect.top + g_nFontHeight;

    DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
    char sText[8] = "";
    if (nAddress <= _6502_STACK_END)
    {
      sprintf(sText, "%04X: ", nAddress);
      PrintTextCursorX(sText, rect);
    }

    if (nAddress <= _6502_STACK_END)
    {
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
      sprintf(sText, "  %02X", (unsigned)*(uint8_t*)(mem + nAddress));
      PrintTextCursorX(sText, rect);
    }
    iStack++;
  }
}

void DrawSourceLine(int iSourceLine, RECT &rect)
{
  char sLine[CONSOLE_WIDTH];
  memset(sLine, 0, CONSOLE_WIDTH);

  if ((iSourceLine >= 0) && (iSourceLine < g_AssemblerSourceBuffer.GetNumLines()))
  {
    char * pSource = g_AssemblerSourceBuffer.GetLine(iSourceLine);
    TextConvertTabsToSpaces(sLine, pSource, CONSOLE_WIDTH - 1);
  }
  else
  {
    strcpy(sLine, " ");
  }

  PrintText(sLine, rect);
  rect.top += g_nFontHeight;
}

void DrawSubWindow_Code(int iWindow)
{
  (void)iWindow;
  int nLines = g_nDisasmWinHeight;

#if !USE_APPLE_FONT
  SelectObject(GetDebuggerMemDC(), g_aFontConfig[FONT_DISASM_DEFAULT]._hFont);
#endif

  unsigned short nAddress = g_nDisasmTopAddress;
  for (int iLine = 0; iLine < nLines; iLine++)
  {
    nAddress += DrawDisassemblyLine(iLine, nAddress);
  }

#if !USE_APPLE_FONT
  SelectObject(GetDebuggerMemDC(), g_aFontConfig[FONT_INFO]._hFont);
#endif
}

void DrawSubWindow_Source(Update_t bUpdate)
{
  (void)bUpdate;
  int nLines = g_nDisasmWinHeight;

  RECT rect;
  rect.left = 0;
  rect.top = 0;
  rect.right = DISPLAY_DISASM_RIGHT;
  rect.bottom = rect.top + g_nFontHeight;

  int iSourceDisplayStart = 0; // TODO: Extern
  int iSourceLine = iSourceDisplayStart;

  for (int iLine = 0; iLine < nLines; iLine++)
  {
    DrawSourceLine(iSourceLine, rect);
    iSourceLine++;
  }
}

void DrawWindow_Code(Update_t bUpdate)
{
  DrawSubWindow_Code(g_iWindowThis);
  DrawWindowBottom(bUpdate, g_iWindowThis);
  DrawSubWindow_Info(bUpdate, g_iWindowThis);
}

void DrawWindow_Source(Update_t bUpdate)
{
  DrawSubWindow_Source(g_iWindowThis);
  DrawWindowBottom(bUpdate, g_iWindowThis);
  DrawSubWindow_Info(bUpdate, g_iWindowThis);
}
