#include "Common.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <cstring>

#include "Debug.h"
#include "Structs.h"
#include "CPU.h"
#include "Memory.h"
#include "Debugger_Display.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Symbols.h"
#include "Debugger_Color.h"
#include "Debugger_Assembler.h"
#include "Mockingboard.h"
#include "Video.h"
#include "SDL3/SDL.h"

// Externs for globals
extern int g_iWindowThis;
extern int g_nFontHeight;
extern int g_nDisplayMemoryLines;
extern bool g_bConfigDisasmAddressColon;
extern bool g_bConfigInfoTargetPointer;
extern int g_nConsoleDisplayWidth;
extern std::string g_aSourceFileName;
extern int g_nDisasmWinHeight;

// Constants from Debugger_Display.cpp
const int DISPLAY_MINIMEM_COLUMN = 357;
const int DISPLAY_WIDTH = 560;
const int DISPLAY_REGS_COLUMN = 357;
const int DISPLAY_SOFTSWITCH_COLUMN = 357;
const int DISPLAY_TARGETS_COLUMN = 357;
const int DISPLAY_WATCHES_COLUMN = 357;
const int DISPLAY_ZEROPAGE_COLUMN = 357;
const int DISPLAY_VIDEO_SCANNER_COLUMN = 357;
const int MAX_DISPLAY_REGS_LINES = 12;
const int MAX_DISPLAY_ZEROPAGE_LINES = 10;
const int MAX_DISPLAY_TARGET_PTR_LINES = 3;
#define DISPLAY_MEMORY_TITLE 1
#define SOFTSWITCH_LANGCARD 1

// Function prototypes for helpers in other files
extern void ColorizeFlags(bool bSet, int bg = BG_INFO, int fg = FG_INFO_REG);

// --- Functions moved from Debugger_Display.cpp ---

void DrawMemory(int line, int iMemDump)
{
  if ((g_iWindowThis != WINDOW_CODE) && !((g_iWindowThis == WINDOW_DATA)))
    return;

  MemoryDump_t* pMD = &g_aMemDump[iMemDump];
  bool bActive = pMD->bActive;
  if (!bActive)
    return;

  uint16_t       nAddr = pMD->nAddress;
  DEVICE_e     eDevice = pMD->eDevice;
  MemoryView_e iView = pMD->eView;

  SS_CARD_MOCKINGBOARD SS_MB;

  if ((eDevice == DEV_SY6522) || (eDevice == DEV_AY8910))
    MB_GetSnapshot(&SS_MB, 4 + (nAddr >> 1));

  RECT rect;
  rect.left = DISPLAY_MINIMEM_COLUMN;
  rect.top = (line * g_nFontHeight);
  rect.right = DISPLAY_WIDTH;
  rect.bottom = rect.top + g_nFontHeight;

  RECT rect2;
  rect2 = rect;

  const int MAX_MEM_VIEW_TXT = 16;
  char sText[MAX_MEM_VIEW_TXT * 2];

  char sType[8] = "Mem";
  char sAddress[12] = "";

  int iForeground = FG_INFO_OPCODE;
  int iBackground = BG_INFO;

#if DISPLAY_MEMORY_TITLE
  if (eDevice == DEV_SY6522)
  {
    snprintf(sAddress, sizeof(sAddress), "SY#%d", nAddr);
  }
  else if (eDevice == DEV_AY8910)
  {
    snprintf(sAddress, sizeof(sAddress), "AY#%d", nAddr);
  }
  else
  {
    snprintf(sAddress, sizeof(sAddress), "%04X", (unsigned)nAddr);

    if (iView == MEM_VIEW_HEX)
      snprintf(sType, sizeof(sType), "HEX");
    else if (iView == MEM_VIEW_ASCII)
      snprintf(sType, sizeof(sType), "ASCII");
    else
      snprintf(sType, sizeof(sType), "TEXT");
  }

  rect2 = rect;
  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
  DebuggerSetColorBG(DebuggerGetColor(BG_INFO));
  PrintTextCursorX(sType, rect2);

  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
  PrintTextCursorX(" at ", rect2);

  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
  PrintTextCursorY(sAddress, rect2);
#endif

  rect.top = rect2.top;
  rect.bottom = rect2.bottom;

  unsigned short iAddress = nAddr;

  int nLines = g_nDisplayMemoryLines;
  int nCols = 4;

  if (iView != MEM_VIEW_HEX)
  {
    nCols = MAX_MEM_VIEW_TXT;
  }

  if ((eDevice == DEV_SY6522) || (eDevice == DEV_AY8910))
  {
    iAddress = 0;
    nCols = 6;
  }

  rect.right = DISPLAY_WIDTH - 1;

  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));

  for (int iLine = 0; iLine < nLines; iLine++)
  {
    rect2 = rect;

    if (iView == MEM_VIEW_HEX)
    {
      sprintf(sText, "%04X", iAddress);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
      PrintTextCursorX(sText, rect2);

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(":", rect2);
    }

    for (int iCol = 0; iCol < nCols; iCol++)
    {
      DebuggerSetColorBG(DebuggerGetColor(iBackground));
      DebuggerSetColorFG(DebuggerGetColor(iForeground));

      if (eDevice == DEV_SY6522)
      {
        sprintf(sText, "%02X", (unsigned)((unsigned char*)&SS_MB.Unit[nAddr & 1].RegsSY6522)[iAddress]);
        if (iCol & 1)
          DebuggerSetColorFG(DebuggerGetColor(iForeground));
        else
          DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
      }
      else if (eDevice == DEV_AY8910)
      {
        sprintf(sText, "%02X", (unsigned)SS_MB.Unit[nAddr & 1].RegsAY8910[iAddress]);
        if (iCol & 1)
          DebuggerSetColorFG(DebuggerGetColor(iForeground));
        else
          DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
      }
      else
      {
        unsigned char nData = (unsigned)*(uint8_t*)(mem + iAddress);
        sText[0] = 0;

        if (iView == MEM_VIEW_HEX)
        {
          if ((iAddress >= _6502_IO_BEGIN) && (iAddress <= _6502_IO_END))
          {
            DebuggerSetColorFG(DebuggerGetColor(FG_INFO_IO_BYTE));
          }

          sprintf(sText, "%02X ", nData);
        }
        else
        {
          if ((iAddress >= _6502_IO_BEGIN) && (iAddress <= _6502_IO_END))
            iBackground = BG_INFO_IO_BYTE;

          ColorizeSpecialChar(sText, nData, iView, iBackground);
        }
      }
      int nChars = PrintTextCursorX(sText, rect2);
      (void)nChars;
      iAddress++;
    }

    rect.top += g_nFontHeight;
    rect.bottom += g_nFontHeight;
  }
}

void DrawRegister(int line, const char* name, const int nBytes, const unsigned short nValue, int iSource)
{
  if ((g_iWindowThis != WINDOW_CODE) && !((g_iWindowThis == WINDOW_DATA)))
    return;

  int nFontWidth = g_aFontConfig[FONT_INFO]._nFontWidthAvg;

  RECT rect;
  rect.top = line * g_nFontHeight;
  rect.bottom = rect.top + g_nFontHeight;
  rect.left = DISPLAY_REGS_COLUMN;
  rect.right = rect.left + (10 * nFontWidth);

  if ((PARAM_REG_A == iSource) ||
    (PARAM_REG_X == iSource) ||
    (PARAM_REG_Y == iSource) ||
    (PARAM_REG_PC == iSource) ||
    (PARAM_REG_SP == iSource))
  {
    DebuggerSetColorFG(DebuggerGetColor(FG_INFO_REG));
  }
  else
  {
    rect.left += nFontWidth;
  }

  int iBackground = BG_DATA_1;
  DebuggerSetColorBG(DebuggerGetColor(iBackground));
  PrintTextCursorX(name, rect);

  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
  PrintTextCursorX(":", rect);

  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
  char sValue[8];
  if (nBytes == 1) sprintf(sValue, "%02X", nValue & 0xFF);
  else             sprintf(sValue, "%04X", nValue);
  PrintTextCursorX(sValue, rect);
}

void DrawRegisters(int line)
{
  const char **sReg = g_aBreakpointSource;

  DrawRegister(line++, sReg[BP_SRC_REG_A], 1, regs.a, PARAM_REG_A);
  DrawRegister(line++, sReg[BP_SRC_REG_X], 1, regs.x, PARAM_REG_X);
  DrawRegister(line++, sReg[BP_SRC_REG_Y], 1, regs.y, PARAM_REG_Y);
  DrawRegister(line++, sReg[BP_SRC_REG_PC], 2, regs.pc, PARAM_REG_PC);
  DrawFlags(line, regs.ps, NULL);
  line += 2;
  DrawRegister(line++, sReg[BP_SRC_REG_S], 2, regs.sp, PARAM_REG_SP);
}

void _DrawSoftSwitchHighlight(RECT & temp, bool bSet, const char *sOn, const char *sOff, int bg = BG_INFO)
{
  ColorizeFlags(bSet, bg);
  PrintTextCursorX(sOn, temp);

  DebuggerSetColorBG(DebuggerGetColor(bg));
  DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
  PrintTextCursorX("/", temp);

  ColorizeFlags(!bSet, bg);
  PrintTextCursorX(sOff, temp);
}

void _DrawSoftSwitchAddress(RECT & rect, int nAddress, int bg_default = BG_INFO)
{
  char sText[4] = "";

  DebuggerSetColorBG(DebuggerGetColor(bg_default));
  DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_TARGET));
  sprintf(sText, "%02X", (nAddress & 0xFF));
  PrintTextCursorX(sText, rect);

  DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
  PrintTextCursorX(":", rect);
}

void _DrawSoftSwitch(RECT & rect, int nAddress, bool bSet, char *sPrefix, char *sOn, char *sOff, const char *sSuffix = NULL, int bg_default = BG_INFO)
{
  RECT temp = rect;

  _DrawSoftSwitchAddress(temp, nAddress, bg_default);

  if (sPrefix)
  {
    DebuggerSetColorFG(DebuggerGetColor(FG_INFO_REG));
    PrintTextCursorX(sPrefix, temp);
  }

  _DrawSoftSwitchHighlight(temp, bSet, sOn, sOff, bg_default);

  DebuggerSetColorBG(DebuggerGetColor(bg_default));
  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
  if (sSuffix)
    PrintTextCursorX(sSuffix, temp);

  rect.top += g_nFontHeight;
  rect.bottom += g_nFontHeight;
}

void _DrawTriStateSoftSwitch(RECT & rect, int nAddress, const int iBankDisplay, int iActive, char *sPrefix, char *sOn, char *sOff, const char *sSuffix = NULL, int bg_default = BG_INFO)
{
  (void)sPrefix;
  (void)sSuffix;
  bool bSet = (iBankDisplay == iActive);

  if (bSet)
    _DrawSoftSwitch(rect, nAddress, bSet, NULL, sOn, sOff, " ", bg_default);
  else
  {
    RECT temp = rect;
    int iBank = (GetMemMode() & MF_HRAM_BANK2)
      ? 2
      : 1
      ;
    bool bDisabled = ((iActive == 0) && (iBank == iBankDisplay));


    _DrawSoftSwitchAddress(temp, nAddress, bg_default);

    DebuggerSetColorBG(DebuggerGetColor(bg_default));
    if (bDisabled)
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
    else
      DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));

    PrintTextCursorX(sOn, temp);
    PrintTextCursorX("/", temp);

    ColorizeFlags(bDisabled, bg_default, FG_DISASM_OPERATOR);
    PrintTextCursorX(sOff, temp);

    DebuggerSetColorBG(DebuggerGetColor(bg_default));
    DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
    PrintTextCursorX(" ", temp);

    rect.top += g_nFontHeight;
    rect.bottom += g_nFontHeight;
  }
}

void _DrawSoftSwitchLanguageCardBank(RECT & rect, const int iBankDisplay, int bg_default = BG_INFO)
{
  const int w = g_aFontConfig[FONT_DISASM_DEFAULT]._nFontWidthAvg;
  const int dx80 = 7 * w;
  const int dx88 = 8 * w;

  rect.right = rect.left + dx80;

  bool bBankWritable = (GetMemMode() & MF_HRAM_WRITE) ? true : false;
  int iBankActive = (GetMemMode() & MF_HIGHRAM)
    ? (GetMemMode() & MF_HRAM_BANK2)
    ? 2
    : 1
    : 0
    ;

  char sOn[4] = "B#";
  char sOff[4] = "M";
  int nAddress = 0xC080 + (8 * (2 - iBankDisplay));
  sOn[1] = '0' + iBankDisplay;

  _DrawTriStateSoftSwitch(rect, nAddress, iBankDisplay, iBankActive, NULL, sOn, sOff, " ", bg_default);

  rect.top -= g_nFontHeight;
  rect.bottom -= g_nFontHeight;

  if (iBankDisplay == 2)
  {
    rect.left += dx80;
    rect.right += 4 * w;

    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_BP_S_X));
    DebuggerSetColorBG(DebuggerGetColor(bg_default));
    PrintTextCursorX((GetMemMode() & MF_ALTZP) ? "x" : " ", rect);

    const char *pOn = "R";
    const char *pOff = "W";

    _DrawSoftSwitchHighlight(rect, !bBankWritable, pOn, pOff, bg_default);
  }
  else
  {
    assert(iBankDisplay == 1);

    rect.left += dx88;
    rect.right += 4 * w;

    int iActiveBank = -1;
    char sText[4] = "?";

#ifdef RAMWORKS
    { sText[0] = 'r'; iActiveBank = GetRamWorksActiveBank(); }
#endif

    if (iActiveBank >= 0)
    {
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_REG));
      PrintTextCursorX(sText, rect);

      sprintf(sText, "%02X", (iActiveBank & 0x7F));
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
      PrintTextCursorX(sText, rect);
    }
    else
    {
      PrintTextCursorX("   ", rect);
    }
  }

  rect.top += g_nFontHeight;
  rect.bottom += g_nFontHeight;
}

void _DrawSoftSwitchMainAuxBanks(RECT & rect)
{
  RECT temp = rect;
  rect.top += g_nFontHeight;
  rect.bottom += g_nFontHeight;

  int w = g_aFontConfig[FONT_DISASM_DEFAULT]._nFontWidthAvg;
  int dx = 7 * w;

  int  nAddress = 0xC002;
  bool bMainRead = (GetMemMode() & MF_AUXREAD) != 0;

  temp.right = rect.left + dx;
  _DrawSoftSwitch(temp, nAddress, !bMainRead, "R", "m", "x", NULL, BG_DATA_2);

  temp.top -= g_nFontHeight;
  temp.bottom -= g_nFontHeight;
  temp.left += dx;
  temp.right += 3 * w;

  nAddress = 0xC004;
  bool bAuxWrite = (GetMemMode() & MF_AUXWRITE) != 0;
  _DrawSoftSwitch(temp, nAddress, bAuxWrite, "W", "x", "m", NULL, BG_DATA_2);
}

void DrawSoftSwitches(int iSoftSwitch)
{
  RECT rect;
  int nFontWidth = g_aFontConfig[FONT_INFO]._nFontWidthAvg;

  rect.left = DISPLAY_SOFTSWITCH_COLUMN;
  rect.top = iSoftSwitch * g_nFontHeight;
  rect.right = rect.left + (10 * nFontWidth) + 1;
  rect.bottom = rect.top + g_nFontHeight;

  DebuggerSetColorBG(DebuggerGetColor(BG_INFO));
  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));

  bool bSet;

  bSet = !VideoGetSWTEXT();
  _DrawSoftSwitch(rect, 0xC050, bSet, NULL, "GR.", "TEXT");

  bSet = !VideoGetSWMIXED();
  _DrawSoftSwitch(rect, 0xC052, bSet, NULL, "FULL", "MIX");

  bSet = !VideoGetSWPAGE2();
  _DrawSoftSwitch(rect, 0xC054, bSet, "PAGE ", "1", "2");

  bSet = !VideoGetSWHIRES();
  _DrawSoftSwitch(rect, 0xC056, bSet, NULL, "LO", "HI", "RES");

  DebuggerSetColorBG(DebuggerGetColor(BG_INFO));
  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));

  bSet = VideoGetSWDHIRES();
  _DrawSoftSwitch(rect, 0xC05E, bSet, NULL, "DHGR", "HGR");


  int bgMemory = BG_DATA_2;

  bSet = !VideoGetSW80STORE();
  _DrawSoftSwitch(rect, 0xC000, bSet, "80Sto", "0", "1", NULL, bgMemory);

  _DrawSoftSwitchMainAuxBanks(rect);

  bSet = !VideoGetSW80COL();
  _DrawSoftSwitch(rect, 0xC00C, bSet, "Col", "40", "80", NULL, bgMemory);

  bSet = !VideoGetSWAltCharSet();
  _DrawSoftSwitch(rect, 0xC00E, bSet, NULL, "ASC", "MOUS", NULL, bgMemory);

#if SOFTSWITCH_LANGCARD
  DebuggerSetColorBG(DebuggerGetColor(bgMemory));
  _DrawSoftSwitchLanguageCardBank(rect, 2, bgMemory);

  rect.left = DISPLAY_SOFTSWITCH_COLUMN;
  _DrawSoftSwitchLanguageCardBank(rect, 1, bgMemory);
#endif
}

void DrawTargets(int line)
{
  if ((g_iWindowThis != WINDOW_CODE) && !((g_iWindowThis == WINDOW_DATA)))
    return;

  int aTarget[3];
  _6502_GetTargets(regs.pc, &aTarget[0], &aTarget[1], &aTarget[2], NULL);
  GetTargets_IgnoreDirectJSRJMP(mem[regs.pc], aTarget[2]);

  aTarget[1] = aTarget[2];

  RECT rect;
  int nFontWidth = g_aFontConfig[FONT_INFO]._nFontWidthAvg;

  int iAddress = MAX_DISPLAY_TARGET_PTR_LINES;
  while (iAddress--)
  {
    char sAddress[8] = "-none-";
    char sData[8] = "";

    if (aTarget[iAddress] != NO_6502_TARGET)
    {
      sprintf(sAddress, "%04X", aTarget[iAddress]);
      if (iAddress)
        sprintf(sData, "%02X", *(uint8_t*)(mem + aTarget[iAddress]));
      else
        sprintf(sData, "%04X", *(uint16_t*)(mem + aTarget[iAddress]));
    }

    rect.left = DISPLAY_TARGETS_COLUMN;
    rect.top = (line + iAddress) * g_nFontHeight;
    int nColumn = rect.left + (7 * nFontWidth);
    rect.right = nColumn;
    rect.bottom = rect.top + g_nFontHeight;

    if (iAddress == 0)
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
    else
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));

    PrintText(sData, rect);
  }
}

void DrawWatches(int line)
{
  if ((g_iWindowThis != WINDOW_CODE) && !((g_iWindowThis == WINDOW_DATA)))
    return;

  RECT rect;
  rect.left = DISPLAY_WATCHES_COLUMN;
  rect.top = (line * g_nFontHeight);
  rect.right = DISPLAY_WIDTH;
  rect.bottom = rect.top + g_nFontHeight;

  char sText[16] = "Watches";

  DebuggerSetColorBG(DebuggerGetColor(BG_INFO_WATCH));

  int iWatch;
  for (iWatch = 0; iWatch < MAX_WATCHES; iWatch++)
  {
    if (g_aWatches[iWatch].bEnabled)
    {
      RECT rect2 = rect;

      DebuggerSetColorBG(DebuggerGetColor(BG_INFO_WATCH));
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
      PrintTextCursorX("W", rect2);

      sprintf(sText, "%X ", iWatch);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_BULLET));
      PrintTextCursorX(sText, rect2);

      sprintf(sText, "%04X", g_aWatches[iWatch].nAddress);
      DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));
      PrintTextCursorX(sText, rect2);

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(":", rect2);

      unsigned char nTarget8 = 0;

      nTarget8 = (unsigned)*(uint8_t*)(mem + g_aWatches[iWatch].nAddress);
      sprintf(sText, "%02X", nTarget8);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
      PrintTextCursorX(sText, rect2);

      nTarget8 = (unsigned)*(uint8_t*)(mem + g_aWatches[iWatch].nAddress + 1);
      sprintf(sText, "%02X", nTarget8);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
      PrintTextCursorX(sText, rect2);

      sprintf(sText, "(");
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(sText, rect2);

      unsigned short nTarget16 = (unsigned)*(uint16_t*)(mem + g_aWatches[iWatch].nAddress);
      sprintf(sText, "%04X", nTarget16);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
      PrintTextCursorX(sText, rect2);

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(")", rect2);

      rect.top += g_nFontHeight;
      rect.bottom += g_nFontHeight;

      rect2 = rect;

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
      for (int iByte = 0; iByte < 8; iByte++)
      {
        if ((iByte & 3) == 0) {
          DebuggerSetColorBG(DebuggerGetColor(BG_INFO_WATCH));
          PrintTextCursorX(" ", rect2);
        }

        if ((iByte & 1) == 1)
          DebuggerSetColorBG(DebuggerGetColor(BG_INFO_WATCH));
        else
          DebuggerSetColorBG(DebuggerGetColor(BG_DATA_2));

        unsigned char nValue8 = (unsigned)*(uint8_t*)(mem + nTarget16 + iByte);
        sprintf(sText, "%02X", nValue8);
        PrintTextCursorX(sText, rect2);
      }
    }
    rect.top += g_nFontHeight;
    rect.bottom += g_nFontHeight;
  }
}

void DrawZeroPagePointers(int line)
{
  if ((g_iWindowThis != WINDOW_CODE) && !((g_iWindowThis == WINDOW_DATA)))
    return;

  int nFontWidth = g_aFontConfig[FONT_INFO]._nFontWidthAvg;

  RECT rect;
  rect.top = line * g_nFontHeight;
  rect.bottom = rect.top + g_nFontHeight;
  rect.left = DISPLAY_ZEROPAGE_COLUMN;
  rect.right = rect.left + (10 * nFontWidth);

  DebuggerSetColorBG(DebuggerGetColor(BG_INFO_ZEROPAGE));

  const int nMaxSymbolLen = 7;
  char sText[nMaxSymbolLen + 1] = "";

  for (int iZP = 0; iZP < MAX_ZEROPAGE_POINTERS; iZP++)
  {
    RECT rect2 = rect;

    Breakpoint_t *pZP = &g_aZeroPagePointers[iZP];
    bool bEnabled = pZP->bEnabled;

    if (bEnabled)
    {
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
      PrintTextCursorX("Z", rect2);

      sprintf(sText, "%X ", iZP);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_BULLET));
      PrintTextCursorX(sText, rect2);

      unsigned char nZPAddr1 = (g_aZeroPagePointers[iZP].nAddress) & 0xFF;
      unsigned char nZPAddr2 = (g_aZeroPagePointers[iZP].nAddress + 1) & 0xFF;

      const char* pSymbol2 = GetSymbol(nZPAddr2, 2);
      const char* pSymbol1 = GetSymbol(nZPAddr1, 2);

      int nLen1 = strlen(pSymbol1);
      int nLen2 = strlen(pSymbol2);

      DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));

      int x;
      for (x = 0; x < nMaxSymbolLen; x++)
      {
        sText[x] = ' ';
      }
      sText[nMaxSymbolLen] = 0;

      if ((nLen1) && (pSymbol1[0] == '$'))
      {
      }
      else
      if ((nLen2) && (pSymbol2[0] == '$'))
      {
        DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));
      }
      else
      {
        int nMin = std::min(nLen1, nMaxSymbolLen);
        memcpy(sText, pSymbol1, nMin);
        DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_SYMBOL));
      }
      PrintText(sText, rect2);

      rect2.left = rect.left;
      rect2.top += g_nFontHeight;
      rect2.bottom += g_nFontHeight;

      sprintf(sText, "%02X", nZPAddr1);
      DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));
      PrintTextCursorX(sText, rect2);

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(":", rect2);

      unsigned short nTarget16 = (unsigned short)mem[nZPAddr1] | ((unsigned short)mem[nZPAddr2] << 8);
      sprintf(sText, "%04X", nTarget16);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
      PrintTextCursorX(sText, rect2);

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(":", rect2);

      unsigned char nValue8 = (unsigned)*(uint8_t*)(mem + nTarget16);
      sprintf(sText, "%02X", nValue8);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
      PrintTextCursorX(sText, rect2);
    }
    rect.top += (g_nFontHeight * 2);
    rect.bottom += (g_nFontHeight * 2);
  }
}

void DrawSubWindow_Data(Update_t bUpdate)
{
  (void)bUpdate;
  int iBackground;

  const int nMaxOpcodes = WINDOW_DATA_BYTES_PER_LINE;
  char  sAddress[5];

  char sOpcodes[CONSOLE_WIDTH] = "";
  char sImmediate[4];

  const int nDefaultFontWidth = 7;
  int X_OPCODE = 6 * nDefaultFontWidth;
  int X_CHAR = (6 + (nMaxOpcodes * 3)) * nDefaultFontWidth;

  int iMemDump = 0;

  MemoryDump_t* pMD = &g_aMemDump[iMemDump];
  uint16_t       nAddress = pMD->nAddress;

  RECT rect;
  rect.top = 0 + 0;

  int  iByte;
  unsigned short iAddress = nAddress;

  int iLine;
  int nLines = g_nDisasmWinHeight;

  for (iLine = 0; iLine < nLines; iLine++)
  {
    iAddress = nAddress;

    sprintf(sAddress, "%04X", iAddress);

    sOpcodes[0] = 0;
    for (iByte = 0; iByte < nMaxOpcodes; iByte++)
    {
      unsigned char nData = (unsigned)*(uint8_t*)(mem + iAddress + iByte);
      sprintf(&sOpcodes[iByte * 3], "%02X ", nData);
    }
    sOpcodes[nMaxOpcodes * 3] = 0;

    int nFontHeight = g_aFontConfig[FONT_DISASM_DEFAULT]._nLineHeight;

    rect.left = 0;
    const int DISPLAY_DISASM_RIGHT = 353;
    rect.right = DISPLAY_DISASM_RIGHT;
    rect.bottom = rect.top + nFontHeight;

    if (iLine & 1)
    {
      iBackground = BG_DATA_1;
    }
    else
    {
      iBackground = BG_DATA_2;
    }
    DebuggerSetColorBG(DebuggerGetColor(iBackground));

    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));
    PrintTextCursorX((const char*)sAddress, rect);

    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    if (g_bConfigDisasmAddressColon)
      PrintTextCursorX(":", rect);

    rect.left = X_OPCODE;

    DebuggerSetColorFG(DebuggerGetColor(FG_DATA_BYTE));
    PrintTextCursorX((const char*)sOpcodes, rect);

    rect.left = X_CHAR;

    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    PrintTextCursorX("  |  ", rect);


    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_CHAR));

    MemoryView_e eView = pMD->eView;
    if ((eView != MEM_VIEW_ASCII) && (eView != MEM_VIEW_APPLE))
      eView = MEM_VIEW_ASCII;

    iAddress = nAddress;
    for (iByte = 0; iByte < nMaxOpcodes; iByte++)
    {
      unsigned char nImmediate = (unsigned)*(uint8_t*)(mem + iAddress);

      ColorizeSpecialChar(sImmediate, (uint8_t)nImmediate, eView, iBackground);
      PrintTextCursorX((const char*)sImmediate, rect);

      iAddress++;
    }
    DebuggerSetColorBG(DebuggerGetColor(iBackground));

    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    PrintTextCursorX("  |  ", rect);

    nAddress += nMaxOpcodes;

    rect.top += nFontHeight;
  }
}

void DrawSubWindow_Symbols(Update_t bUpdate)
{
  (void)bUpdate;
}

void DrawSubWindow_ZeroPage(Update_t bUpdate)
{
  (void)bUpdate;
}

void DrawWindow_Data(Update_t bUpdate)
{
  DrawSubWindow_Data(g_iWindowThis);
  DrawSubWindow_Info(bUpdate, g_iWindowThis);
}

void DrawWindow_IO(Update_t bUpdate)
{
  DrawSubWindow_IO(g_iWindowThis);
  DrawSubWindow_Info(bUpdate, g_iWindowThis);
}

void DrawWindow_Symbols(Update_t bUpdate)
{
  DrawSubWindow_Symbols(g_iWindowThis);
  DrawSubWindow_Info(bUpdate, g_iWindowThis);
}

void DrawWindow_ZeroPage(Update_t bUpdate)
{
  DrawSubWindow_ZeroPage(bUpdate);
  DrawSubWindow_Info(bUpdate, g_iWindowThis);
}

void DrawVideoScannerValue(int line, int vert, int horz, bool isVisible)
{
  if ((g_iWindowThis != WINDOW_CODE) && !((g_iWindowThis == WINDOW_DATA)))
    return;

  const int nFontWidth = g_aFontConfig[FONT_INFO]._nFontWidthAvg;

  const int nameWidth = 2; // 2 chars
  const int numberWidth = 3; // 3 chars
  const int gapWidth = 1;   // 1 space
  const int totalWidth = (nameWidth + numberWidth) * 2 + gapWidth;

  RECT rect;
  rect.top = line * g_nFontHeight;
  rect.bottom = rect.top + g_nFontHeight;
  rect.left = DISPLAY_VIDEO_SCANNER_COLUMN;
  rect.right = rect.left + (totalWidth * nFontWidth);

  for (int i = 0; i < 2; i++)
  {
    DebuggerSetColorBG(DebuggerGetColor(BG_VIDEOSCANNER_TITLE));
    DebuggerSetColorFG(DebuggerGetColor(FG_VIDEOSCANNER_TITLE));

    const int nValue = (i == 0) ? vert : horz;

    if (i == 0) PrintText("v:", rect);
    else        PrintText("h:", rect);
    rect.left += nameWidth * nFontWidth;

    char sValue[8];
    if (g_videoScannerDisplayInfo.isDecimal)
      snprintf(sValue, sizeof(sValue), "%03u", nValue);
    else
      snprintf(sValue, sizeof(sValue), "%03X", nValue);

    if (!isVisible)
      DebuggerSetColorFG(DebuggerGetColor(FG_VIDEOSCANNER_INVISIBLE)); // red
    else
      DebuggerSetColorFG(DebuggerGetColor(FG_VIDEOSCANNER_VISIBLE));   // green
    PrintText(sValue, rect);
    rect.left += (numberWidth + gapWidth) * nFontWidth;
  }
}

void DrawVideoScannerInfo (int line)
{
  (void)line;
#ifdef TODO // Not supported for Linux yet
  // NTSC_VideoGetScannerAddressForDebugger();    // update g_nVideoClockHorz/g_nVideoClockVert
  int v = 0;
  int h = 0;
  DrawVideoScannerValue(line, v, h, true);
#endif
}
