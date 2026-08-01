#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Color.h"
#include "Debugger_Console.h"
#include "Debugger_Display.h"
#include "Debugger_Parser.h"
#include "Debugger_Symbols.h"
#include "Video.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

// Externs for globals
extern int g_window_this;
extern int g_font_height;
extern int g_display_memory_lines;
extern bool g_config_disasm_address_colon;
extern bool g_config_info_target_pointer;
extern int g_console_display_width;
extern std::string g_source_file_name;
extern int g_disasm_win_height;

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

void DrawMemory(int line, int iMemDump) {
  if ((g_window_this != WINDOW_CODE) && !((g_window_this == WINDOW_DATA))) {
    return;
  }

  MemoryDump_t* pMD = &g_mem_dump[iMemDump];
  bool bActive = pMD->bActive;
  if (!bActive) {
    return;
  }

  uint16_t nAddr = pMD->nAddress;
  DEVICE_e eDevice = pMD->eDevice;
  MemoryView_e iView = pMD->eView;

  Rect_t rect;
  rect.left = DISPLAY_MINIMEM_COLUMN;
  rect.top = (line * g_font_height);
  rect.right = DISPLAY_WIDTH;
  rect.bottom = rect.top + g_font_height;

  Rect_t rect2;
  rect2 = rect;

  const int MAX_MEM_VIEW_TXT = 16;
  char sText[MAX_MEM_VIEW_TXT * 2];

  char sType[8] = "Mem";
  char sAddress[12] = "";

  int iForeground = FG_INFO_OPCODE;
  int iBackground = BG_INFO;

#if DISPLAY_MEMORY_TITLE
  snprintf(sAddress, sizeof(sAddress), "%04X", static_cast<unsigned>(nAddr));

  if (iView == MEM_VIEW_HEX) {
    snprintf(sType, sizeof(sType), "HEX");
  } else if (iView == MEM_VIEW_ASCII) {
    snprintf(sType, sizeof(sType), "ASCII");
  } else {
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

  uint16_t iAddress = nAddr;

  int nLines = g_display_memory_lines;
  int nCols = 4;

  if (iView != MEM_VIEW_HEX) {
    nCols = MAX_MEM_VIEW_TXT;
  }

  if ((eDevice == DEV_SY6522) || (eDevice == DEV_AY8910)) {
    iAddress = 0;
    nCols = 6;
  }

  rect.right = DISPLAY_WIDTH - 1;

  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));

  for (int iLine = 0; iLine < nLines; iLine++) {
    rect2 = rect;

    if (iView == MEM_VIEW_HEX) {
      sprintf(sText, "%04X", iAddress);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
      PrintTextCursorX(sText, rect2);

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(":", rect2);
    }

    for (int iCol = 0; iCol < nCols; iCol++) {
      DebuggerSetColorFG(DebuggerGetColor(iForeground));

      uint8_t nData = static_cast<unsigned>(*(mem + iAddress));
      sText[0] = 0;

      if (iView == MEM_VIEW_HEX) {
        if ((iAddress >= _6502_IO_BEGIN) && (iAddress <= _6502_IO_END)) {
          DebuggerSetColorFG(DebuggerGetColor(FG_INFO_IO_BYTE));
        }

        sprintf(sText, "%02X ", nData);
      } else {
        if ((iAddress >= _6502_IO_BEGIN) && (iAddress <= _6502_IO_END)) {
          iBackground = BG_INFO_IO_BYTE;
        }

        ColorizeSpecialChar(sText, nData, iView, iBackground);
      }

      int nChars = PrintTextCursorX(sText, rect2);
      (void)nChars;
      iAddress++;
    }

    rect.top += g_font_height;
    rect.bottom += g_font_height;
  }
}

void DrawRegister(int line, const char* name, const int nBytes,
                  const uint16_t nValue, int iSource) {
  if ((g_window_this != WINDOW_CODE) && !((g_window_this == WINDOW_DATA))) {
    return;
  }

  int nFontWidth = g_font_config[FONT_INFO]._nFontWidthAvg;

  Rect_t rect;
  rect.top = line * g_font_height;
  rect.bottom = rect.top + g_font_height;
  rect.left = DISPLAY_REGS_COLUMN;
  rect.right = rect.left + (10 * nFontWidth);

  if ((PARAM_REG_A == iSource) || (PARAM_REG_X == iSource) ||
      (PARAM_REG_Y == iSource) || (PARAM_REG_PC == iSource) ||
      (PARAM_REG_SP == iSource)) {
    DebuggerSetColorFG(DebuggerGetColor(FG_INFO_REG));
  } else {
    rect.left += nFontWidth;
  }

  int iBackground = BG_DATA_1;
  DebuggerSetColorBG(DebuggerGetColor(iBackground));
  PrintTextCursorX(name, rect);

  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
  PrintTextCursorX(":", rect);

  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
  char sValue[8];
  if (nBytes == 1) {
    sprintf(sValue, "%02X", nValue & 0xFF);
  } else {
    sprintf(sValue, "%04X", nValue);
  }
  PrintTextCursorX(sValue, rect);
}

void DrawRegisters(int line) {
  const char** sReg = g_breakpoint_source;
  printf("DrawRegisters: line=%d start\n", line);

  DrawRegister(line++, sReg[BP_SRC_REG_A], 1, cpu_get_registers()->a,
               PARAM_REG_A);
  DrawRegister(line++, sReg[BP_SRC_REG_X], 1, cpu_get_registers()->x,
               PARAM_REG_X);
  DrawRegister(line++, sReg[BP_SRC_REG_Y], 1, cpu_get_registers()->y,
               PARAM_REG_Y);
  DrawRegister(line++, sReg[BP_SRC_REG_PC], 2, cpu_get_registers()->pc,
               PARAM_REG_PC);
  DrawFlags(line, cpu_get_registers()->ps, nullptr);
  line += 2;
  DrawRegister(line++, sReg[BP_SRC_REG_S], 2, cpu_get_registers()->sp,
               PARAM_REG_SP);
  printf("DrawRegisters: end\n");
}

void DrawSoftSwitchHighlight(Rect_t& temp, bool bSet, const char* sOn,
                             const char* sOff, int bg = BG_INFO) {
  ColorizeFlags(bSet, bg);
  PrintTextCursorX(sOn, temp);

  DebuggerSetColorBG(DebuggerGetColor(bg));
  DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
  PrintTextCursorX("/", temp);

  ColorizeFlags(!bSet, bg);
  PrintTextCursorX(sOff, temp);
}

void DrawSoftSwitchAddress(Rect_t& rect, int nAddress,
                           int bg_default = BG_INFO) {
  char sText[4] = "";

  DebuggerSetColorBG(DebuggerGetColor(bg_default));
  DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_TARGET));
  sprintf(sText, "%02X", (nAddress & 0xFF));
  PrintTextCursorX(sText, rect);

  DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
  PrintTextCursorX(":", rect);
}

void DrawSoftSwitch(Rect_t& rect, int nAddress, bool bSet, const char* sPrefix,
                    const char* sOn, const char* sOff,
                    const char* sSuffix = nullptr, int bg_default = BG_INFO) {
  Rect_t temp = rect;

  DrawSoftSwitchAddress(temp, nAddress, bg_default);

  if (sPrefix) {
    DebuggerSetColorFG(DebuggerGetColor(FG_INFO_REG));
    PrintTextCursorX(sPrefix, temp);
  }

  DrawSoftSwitchHighlight(temp, bSet, sOn, sOff, bg_default);

  DebuggerSetColorBG(DebuggerGetColor(bg_default));
  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
  if (sSuffix) {
    PrintTextCursorX(sSuffix, temp);
  }

  rect.top += g_font_height;
  rect.bottom += g_font_height;
}

void DrawTriStateSoftSwitch(Rect_t& rect, int nAddress, const int iBankDisplay,
                            int iActive, char* sPrefix, char* sOn, char* sOff,
                            const char* sSuffix = nullptr,
                            int bg_default = BG_INFO) {
  (void)sPrefix;
  (void)sSuffix;
  bool bSet = (iBankDisplay == iActive);

  if (bSet) {
    DrawSoftSwitch(rect, nAddress, bSet, nullptr, sOn, sOff, " ", bg_default);
  } else {
    Rect_t temp = rect;
    int iBank = (get_mem_mode() & MF_HRAM_BANK2) ? 2 : 1;
    bool bDisabled = ((iActive == 0) && (iBank == iBankDisplay));

    DrawSoftSwitchAddress(temp, nAddress, bg_default);

    DebuggerSetColorBG(DebuggerGetColor(bg_default));
    if (bDisabled) {
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
    } else {
      DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    }

    PrintTextCursorX(sOn, temp);
    PrintTextCursorX("/", temp);

    ColorizeFlags(bDisabled, bg_default, FG_DISASM_OPERATOR);
    PrintTextCursorX(sOff, temp);

    DebuggerSetColorBG(DebuggerGetColor(bg_default));
    DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
    PrintTextCursorX(" ", temp);

    rect.top += g_font_height;
    rect.bottom += g_font_height;
  }
}

void DrawSoftSwitchLanguageCardBank(Rect_t& rect, const int iBankDisplay,
                                    int bg_default = BG_INFO) {
  const int w = g_font_config[FONT_DISASM_DEFAULT]._nFontWidthAvg;
  const int dx80 = 7 * w;
  const int dx88 = 8 * w;

  rect.right = rect.left + dx80;

  bool bBankWritable = (get_mem_mode() & MF_HRAM_WRITE) ? true : false;
  int iBankActive =
      (get_mem_mode() & MF_HIGHRAM) ? (get_mem_mode() & MF_HRAM_BANK2) ? 2 : 1 : 0;

  char sOn[4] = "B#";
  char sOff[4] = "M";
  int nAddress = 0xC080 + (8 * (2 - iBankDisplay));
  sOn[1] = '0' + iBankDisplay;

  DrawTriStateSoftSwitch(rect, nAddress, iBankDisplay, iBankActive, nullptr,
                         sOn, sOff, " ", bg_default);

  rect.top -= g_font_height;
  rect.bottom -= g_font_height;

  if (iBankDisplay == 2) {
    rect.left += dx80;
    rect.right += 4 * w;

    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_BP_S_X));
    DebuggerSetColorBG(DebuggerGetColor(bg_default));
    PrintTextCursorX((get_mem_mode() & MF_ALTZP) ? "x" : " ", rect);

    const char* pOn = "R";
    const char* pOff = "W";

    DrawSoftSwitchHighlight(rect, !bBankWritable, pOn, pOff, bg_default);
  } else {
    assert(iBankDisplay == 1);

    rect.left += dx88;
    rect.right += 4 * w;

    int iActiveBank = -1;
    char sText[4] = "?";

#ifdef RAMWORKS
    {
      sText[0] = 'r';
      iActiveBank = get_ramworks_active_bank();
    }
#endif

    if (iActiveBank >= 0) {
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_REG));
      PrintTextCursorX(sText, rect);

      sprintf(sText, "%02X", (iActiveBank & 0x7F));
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
      PrintTextCursorX(sText, rect);
    } else {
      PrintTextCursorX("   ", rect);
    }
  }

  rect.top += g_font_height;
  rect.bottom += g_font_height;
}

void DrawSoftSwitchMainAuxBanks(Rect_t& rect) {
  Rect_t temp = rect;
  rect.top += g_font_height;
  rect.bottom += g_font_height;

  int w = g_font_config[FONT_DISASM_DEFAULT]._nFontWidthAvg;
  int dx = 7 * w;

  int nAddress = 0xC002;
  bool bMainRead = (get_mem_mode() & MF_AUXREAD) != 0;

  temp.right = rect.left + dx;
  DrawSoftSwitch(temp, nAddress, !bMainRead, "R", "m", "x", nullptr, BG_DATA_2);

  temp.top -= g_font_height;
  temp.bottom -= g_font_height;
  temp.left += dx;
  temp.right += 3 * w;

  nAddress = 0xC004;
  bool bAuxWrite = (get_mem_mode() & MF_AUXWRITE) != 0;
  DrawSoftSwitch(temp, nAddress, bAuxWrite, "W", "x", "m", nullptr, BG_DATA_2);
}

void DrawSoftSwitches(int iSoftSwitch) {
  Rect_t rect;
  int nFontWidth = g_font_config[FONT_INFO]._nFontWidthAvg;

  rect.left = DISPLAY_SOFTSWITCH_COLUMN;
  rect.top = iSoftSwitch * g_font_height;
  rect.right = rect.left + (10 * nFontWidth) + 1;
  rect.bottom = rect.top + g_font_height;

  DebuggerSetColorBG(DebuggerGetColor(BG_INFO));
  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));

  bool bSet = false;

  bSet = !video_get_sw_text();
  DrawSoftSwitch(rect, 0xC050, bSet, nullptr, "GR.", "TEXT");

  bSet = !video_get_sw_mixed();
  DrawSoftSwitch(rect, 0xC052, bSet, nullptr, "FULL", "MIX");

  bSet = !video_get_sw_page2();
  DrawSoftSwitch(rect, 0xC054, bSet, "PAGE ", "1", "2");

  bSet = !video_get_sw_hires();
  DrawSoftSwitch(rect, 0xC056, bSet, nullptr, "LO", "HI", "RES");

  DebuggerSetColorBG(DebuggerGetColor(BG_INFO));
  DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));

  bSet = video_get_sw_dhires();
  DrawSoftSwitch(rect, 0xC05E, bSet, nullptr, "DHGR", "HGR");

  int bgMemory = BG_DATA_2;

  bSet = !video_get_sw_80store();
  DrawSoftSwitch(rect, 0xC000, bSet, "80Sto", "0", "1", nullptr, bgMemory);

  DrawSoftSwitchMainAuxBanks(rect);

  bSet = !video_get_sw_80col();
  DrawSoftSwitch(rect, 0xC00C, bSet, "Col", "40", "80", nullptr, bgMemory);

  bSet = !video_get_sw_alt_charset();
  DrawSoftSwitch(rect, 0xC00E, bSet, nullptr, "ASC", "MOUS", nullptr, bgMemory);

#if SOFTSWITCH_LANGCARD
  DebuggerSetColorBG(DebuggerGetColor(bgMemory));
  DrawSoftSwitchLanguageCardBank(rect, 2, bgMemory);

  rect.left = DISPLAY_SOFTSWITCH_COLUMN;
  DrawSoftSwitchLanguageCardBank(rect, 1, bgMemory);
#endif
}

void DrawTargets(int line) {
  if ((g_window_this != WINDOW_CODE) && !((g_window_this == WINDOW_DATA))) {
    return;
  }

  int aTarget[3];
  _6502_GetTargets(cpu_get_registers()->pc, &aTarget[0], &aTarget[1], &aTarget[2],
                   nullptr);
  GetTargets_IgnoreDirectJSRJMP(mem[cpu_get_registers()->pc], aTarget[2]);

  aTarget[1] = aTarget[2];

  Rect_t rect;
  int nFontWidth = g_font_config[FONT_INFO]._nFontWidthAvg;

  int iAddress = MAX_DISPLAY_TARGET_PTR_LINES;
  while (iAddress--) {
    char sAddress[8] = "-none-";
    char sData[8] = "";

    if (aTarget[iAddress] != NO_6502_TARGET) {
      sprintf(sAddress, "%04X", aTarget[iAddress]);
      if (iAddress) {
        sprintf(sData, "%02X", *(mem + aTarget[iAddress]));
      } else {
        uint16_t val16 =
            *(mem + aTarget[iAddress]) |
            (*(mem + static_cast<uint16_t>(aTarget[iAddress] + 1)) << 8);
        sprintf(sData, "%04X", val16);
      }
    }

    rect.left = DISPLAY_TARGETS_COLUMN;
    rect.top = (line + iAddress) * g_font_height;
    int nColumn = rect.left + (7 * nFontWidth);
    rect.right = nColumn;
    rect.bottom = rect.top + g_font_height;

    if (iAddress == 0) {
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
    } else {
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
    }

    PrintText(sData, rect);
  }
}

void DrawWatches(int line) {
  if ((g_window_this != WINDOW_CODE) && !((g_window_this == WINDOW_DATA))) {
    return;
  }

  Rect_t rect;
  rect.left = DISPLAY_WATCHES_COLUMN;
  rect.top = (line * g_font_height);
  rect.right = DISPLAY_WIDTH;
  rect.bottom = rect.top + g_font_height;

  char sText[16] = "Watches";

  DebuggerSetColorBG(DebuggerGetColor(BG_INFO_WATCH));

  int iWatch = 0;
  for (iWatch = 0; iWatch < MAX_WATCHES; iWatch++) {
    if (g_watches[iWatch].bEnabled) {
      Rect_t rect2 = rect;

      DebuggerSetColorBG(DebuggerGetColor(BG_INFO_WATCH));
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
      PrintTextCursorX("W", rect2);

      sprintf(sText, "%X ", iWatch);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_BULLET));
      PrintTextCursorX(sText, rect2);

      sprintf(sText, "%04X", g_watches[iWatch].nAddress);
      DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));
      PrintTextCursorX(sText, rect2);

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(":", rect2);

      uint8_t nTarget8 = 0;

      nTarget8 = static_cast<unsigned>(*(mem + g_watches[iWatch].nAddress));
      sprintf(sText, "%02X", nTarget8);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
      PrintTextCursorX(sText, rect2);

      nTarget8 = static_cast<unsigned>(
          *(mem + static_cast<uint16_t>(g_watches[iWatch].nAddress + 1)));
      sprintf(sText, "%02X", nTarget8);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
      PrintTextCursorX(sText, rect2);

      sprintf(sText, "(");
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(sText, rect2);

      uint16_t nTarget16 =
          *(mem + g_watches[iWatch].nAddress) |
          (*(mem + static_cast<uint16_t>(g_watches[iWatch].nAddress + 1))
           << 8);
      sprintf(sText, "%04X", nTarget16);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
      PrintTextCursorX(sText, rect2);

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(")", rect2);

      rect.top += g_font_height;
      rect.bottom += g_font_height;

      rect2 = rect;

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
      for (int iByte = 0; iByte < 8; iByte++) {
        if ((iByte & 3) == 0) {
          DebuggerSetColorBG(DebuggerGetColor(BG_INFO_WATCH));
          PrintTextCursorX(" ", rect2);
        }

        if ((iByte & 1) == 1) {
          DebuggerSetColorBG(DebuggerGetColor(BG_INFO_WATCH));
        } else {
          DebuggerSetColorBG(DebuggerGetColor(BG_DATA_2));
        }

        uint8_t nValue8 = static_cast<unsigned>(
            *(mem + static_cast<uint16_t>(nTarget16 + iByte)));
        sprintf(sText, "%02X", nValue8);
        PrintTextCursorX(sText, rect2);
      }
    }
    rect.top += g_font_height;
    rect.bottom += g_font_height;
  }
}

void DrawZeroPagePointers(int line) {
  if ((g_window_this != WINDOW_CODE) && !((g_window_this == WINDOW_DATA))) {
    return;
  }

  int nFontWidth = g_font_config[FONT_INFO]._nFontWidthAvg;

  Rect_t rect;
  rect.top = line * g_font_height;
  rect.bottom = rect.top + g_font_height;
  rect.left = DISPLAY_ZEROPAGE_COLUMN;
  rect.right = rect.left + (10 * nFontWidth);

  DebuggerSetColorBG(DebuggerGetColor(BG_INFO_ZEROPAGE));

  const int nMaxSymbolLen = 7;
  char sText[nMaxSymbolLen + 1] = "";

  for (int iZP = 0; iZP < MAX_ZEROPAGE_POINTERS; iZP++) {
    Rect_t rect2 = rect;

    Breakpoint_t* pZP = &g_zero_page_pointers[iZP];
    bool bEnabled = pZP->bEnabled;

    if (bEnabled) {
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_TITLE));
      PrintTextCursorX("Z", rect2);

      sprintf(sText, "%X ", iZP);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_BULLET));
      PrintTextCursorX(sText, rect2);

      uint8_t nZPAddr1 = (g_zero_page_pointers[iZP].nAddress) & 0xFF;
      uint8_t nZPAddr2 = (g_zero_page_pointers[iZP].nAddress + 1) & 0xFF;

      const char* pSymbol2 = GetSymbol(nZPAddr2, 2);
      const char* pSymbol1 = GetSymbol(nZPAddr1, 2);

      int nLen1 = strlen(pSymbol1);
      int nLen2 = strlen(pSymbol2);

      DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));

      int x = 0;
      for (x = 0; x < nMaxSymbolLen; x++) {
        sText[x] = ' ';
      }
      sText[nMaxSymbolLen] = 0;

      if ((nLen1) && (pSymbol1[0] == '$')) {
      } else if ((nLen2) && (pSymbol2[0] == '$')) {
        DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));
      } else {
        int nMin = std::min(nLen1, nMaxSymbolLen);
        memcpy(sText, pSymbol1, nMin);
        DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_SYMBOL));
      }
      PrintText(sText, rect2);

      rect2.left = rect.left;
      rect2.top += g_font_height;
      rect2.bottom += g_font_height;

      sprintf(sText, "%02X", nZPAddr1);
      DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));
      PrintTextCursorX(sText, rect2);

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(":", rect2);

      uint16_t nTarget16 = static_cast<uint16_t>(mem[nZPAddr1]) |
                           (static_cast<uint16_t>(mem[nZPAddr2]) << 8);
      sprintf(sText, "%04X", nTarget16);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_ADDRESS));
      PrintTextCursorX(sText, rect2);

      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPERATOR));
      PrintTextCursorX(":", rect2);

      uint8_t nValue8 = static_cast<unsigned>(*(mem + nTarget16));
      sprintf(sText, "%02X", nValue8);
      DebuggerSetColorFG(DebuggerGetColor(FG_INFO_OPCODE));
      PrintTextCursorX(sText, rect2);
    }
    rect.top += (g_font_height * 2);
    rect.bottom += (g_font_height * 2);
  }
}

void DrawSubWindow_Data(Update_t bUpdate) {
  (void)bUpdate;
  int iBackground = 0;

  const int nMaxOpcodes = WINDOW_DATA_BYTES_PER_LINE;
  char sAddress[5];

  char sOpcodes[CONSOLE_WIDTH] = "";
  char sImmediate[4];

  const int nDefaultFontWidth = 7;
  int X_OPCODE = 6 * nDefaultFontWidth;
  int X_CHAR = (6 + (nMaxOpcodes * 3)) * nDefaultFontWidth;

  int iMemDump = 0;

  MemoryDump_t* pMD = &g_mem_dump[iMemDump];
  uint16_t nAddress = pMD->nAddress;

  Rect_t rect;
  rect.top = 0 + 0;

  int iByte = 0;
  uint16_t iAddress = nAddress;

  int iLine = 0;
  int nLines = g_disasm_win_height;

  for (iLine = 0; iLine < nLines; iLine++) {
    iAddress = nAddress;

    sprintf(sAddress, "%04X", iAddress);

    sOpcodes[0] = 0;
    for (iByte = 0; iByte < nMaxOpcodes; iByte++) {
      uint8_t nData = static_cast<unsigned>(
          *(mem + static_cast<uint16_t>(iAddress + iByte)));
      sprintf(&sOpcodes[static_cast<ptrdiff_t>(iByte * 3)], "%02X ", nData);
    }
    sOpcodes[static_cast<ptrdiff_t>(nMaxOpcodes * 3)] = 0;

    int nFontHeight = g_font_config[FONT_DISASM_DEFAULT]._nLineHeight;

    rect.left = 0;
    const int DISPLAY_DISASM_RIGHT = 353;
    rect.right = DISPLAY_DISASM_RIGHT;
    rect.bottom = rect.top + nFontHeight;

    if (iLine & 1) {
      iBackground = BG_DATA_1;
    } else {
      iBackground = BG_DATA_2;
    }
    DebuggerSetColorBG(DebuggerGetColor(iBackground));

    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_ADDRESS));
    PrintTextCursorX((const char*)sAddress, rect);

    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    if (g_config_disasm_address_colon) {
      PrintTextCursorX(":", rect);
    }

    rect.left = X_OPCODE;

    DebuggerSetColorFG(DebuggerGetColor(FG_DATA_BYTE));
    PrintTextCursorX((const char*)sOpcodes, rect);

    rect.left = X_CHAR;

    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_OPERATOR));
    PrintTextCursorX("  |  ", rect);

    DebuggerSetColorFG(DebuggerGetColor(FG_DISASM_CHAR));

    MemoryView_e eView = pMD->eView;
    if ((eView != MEM_VIEW_ASCII) && (eView != MEM_VIEW_APPLE)) {
      eView = MEM_VIEW_ASCII;
    }

    iAddress = nAddress;
    for (iByte = 0; iByte < nMaxOpcodes; iByte++) {
      uint8_t nImmediate = static_cast<unsigned>(*(mem + iAddress));

      ColorizeSpecialChar(sImmediate, static_cast<uint8_t>(nImmediate), eView,
                          iBackground);
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

void DrawSubWindow_Symbols(Update_t bUpdate) { (void)bUpdate; }

void DrawSubWindow_ZeroPage(Update_t bUpdate) { (void)bUpdate; }

void DrawWindow_Data(Update_t bUpdate) {
  DrawSubWindow_Data(g_window_this);
  DrawSubWindow_Info(bUpdate, g_window_this);
}

void DrawWindow_IO(Update_t bUpdate) {
  DrawSubWindow_IO(g_window_this);
  DrawSubWindow_Info(bUpdate, g_window_this);
}

void DrawWindow_Symbols(Update_t bUpdate) {
  DrawSubWindow_Symbols(g_window_this);
  DrawSubWindow_Info(bUpdate, g_window_this);
}

void DrawWindow_ZeroPage(Update_t bUpdate) {
  DrawSubWindow_ZeroPage(bUpdate);
  DrawSubWindow_Info(bUpdate, g_window_this);
}

void DrawVideoScannerValue(int line, int vert, int horz, bool isVisible) {
  if ((g_window_this != WINDOW_CODE) && !((g_window_this == WINDOW_DATA))) {
    return;
  }

  const int nFontWidth = g_font_config[FONT_INFO]._nFontWidthAvg;

  const int nameWidth = 2;    // 2 chars
  const int numberWidth = 3;  // 3 chars
  const int gapWidth = 1;     // 1 space
  const int totalWidth = (nameWidth + numberWidth) * 2 + gapWidth;

  Rect_t rect;
  rect.top = line * g_font_height;
  rect.bottom = rect.top + g_font_height;
  rect.left = DISPLAY_VIDEO_SCANNER_COLUMN;
  rect.right = rect.left + (totalWidth * nFontWidth);

  for (int i = 0; i < 2; i++) {
    DebuggerSetColorBG(DebuggerGetColor(BG_VIDEOSCANNER_TITLE));
    DebuggerSetColorFG(DebuggerGetColor(FG_VIDEOSCANNER_TITLE));

    const int nValue = (i == 0) ? vert : horz;

    if (i == 0) {
      PrintText("v:", rect);
    } else {
      PrintText("h:", rect);
    }
    rect.left += nameWidth * nFontWidth;

    char sValue[8];
    if (g_video_scanner_display_info.isDecimal) {
      snprintf(sValue, sizeof(sValue), "%03u", nValue);
    } else {
      snprintf(sValue, sizeof(sValue), "%03X", nValue);
    }

    if (!isVisible) {
      DebuggerSetColorFG(DebuggerGetColor(FG_VIDEOSCANNER_INVISIBLE));  // red
    } else {
      DebuggerSetColorFG(DebuggerGetColor(FG_VIDEOSCANNER_VISIBLE));  // green
    }
    PrintText(sValue, rect);
    rect.left += (numberWidth + gapWidth) * nFontWidth;
  }
}

void DrawVideoScannerInfo(int line) {
  (void)line;
#ifdef TODO  // Not supported for Linux yet
  // NTSC_VideoGetScannerAddressForDebugger();    // update
  // g_video_clock_horz/g_video_clock_vert
  int v = 0;
  int h = 0;
  DrawVideoScannerValue(line, v, h, true);
#endif
}
