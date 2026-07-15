#include "Debugger_Display.h"

#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstddef>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Cmd_CPU.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Color.h"
#include "Debugger_Console.h"
#include "Debugger_DisassemblerData.h"
#include "Debugger_Help.h"
#include "Debugger_Memory.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "charset40.xpm"  // US/default
#include "core/Asset.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Util_Path.h"
#include "frontends/common/VideoStretch.h"

enum { DEBUG_FORCE_DISPLAY = 0 };

// Globals __________________________________________________________________

VideoSurface* g_debug_screen = nullptr;
VideoSurface* g_hDebugCharset = nullptr;

ColorRef_t g_hConsoleBrushFG = WHITE;
ColorRef_t g_hConsoleBrushBG = BLACK;

FontConfig_t g_aFontConfig[NUM_FONTS];
char g_aDebuggerVirtualTextScreen[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                 [DEBUG_VIRTUAL_TEXT_WIDTH];
ColorRef_t g_aDebuggerVirtualTextScreenFG[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                         [DEBUG_VIRTUAL_TEXT_WIDTH];
ColorRef_t g_aDebuggerVirtualTextScreenBG[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                         [DEBUG_VIRTUAL_TEXT_WIDTH];

extern int g_iWindowLast;
extern int g_iWindowThis;
extern WindowSplit_t g_aWindowConfig[NUM_WINDOWS];

extern int g_nDisasmWinHeight;
extern int g_nConsoleDisplayLines;
int g_nDisplayMemoryLines = 8;
VideoScannerDisplayInfo g_videoScannerDisplayInfo;

// Prototypes _______________________________________________________________

extern void DisasmInit();
extern auto _CmdSymbolsClear(SymbolTable_Index_e eSymbolTable) -> Update_t;
extern void frame_refresh_status(int);

extern void DrawSubWindow_Symbols(Update_t bUpdate);
extern void DrawSubWindow_ZeroPage(Update_t bUpdate);
extern void DrawSubWindow_Source(Update_t bUpdate);

void DrawSubWindow_IO(Update_t) {}

const int DISPLAY_WIDTH = 560;
const int DISPLAY_DISASM_RIGHT = 353;

// Implementation ___________________________________________________________

#define SOFTSTRECH(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, DST_W, \
                   DST_H)                                                     \
  {                                                                           \
    VideoRect srcrect = {SRC_X, SRC_Y, SRC_W, SRC_H};                         \
    VideoRect dstrect = {DST_X, DST_Y, DST_W, DST_H};                         \
    VideoSoftStretch(SRC, &srcrect, DST, &dstrect);                           \
  }

#define SOFTSTRECH_MONO(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, \
                        DST_W, DST_H)                                       \
  {                                                                         \
    VideoRect srcrect = {SRC_X, SRC_Y, SRC_W, SRC_H};                       \
    VideoRect dstrect = {DST_X, DST_Y, DST_W, DST_H};                       \
    VideoSoftStretchMono8(SRC, &srcrect, DST, &dstrect, hBrush, hBgBrush);  \
  }

//===========================================================================

void AllocateDebuggerMemDC() {
  if (!g_debug_screen) {
    g_debug_screen = VideoCreateSurface(560, 384, 1);
    if (g_debug_screen) {
      VideoColor* pal = VideoGetOutputPalette();
      if (pal) {
        memcpy(g_debug_screen->palette, pal, 256 * sizeof(VideoColor));
      }
    }
    g_hDebugCharset = VideoLoadXPM(charset40_xpm);
  }
}

void ReleaseDebuggerMemDC() {}

void GetDebugViewPortScale(float* x, float* y) {
  if (!g_debug_screen) {
    *x = 1.0f;
    *y = 1.0f;
    return;
  }
  float f = (static_cast<float>(g_debug_screen->w)) / SCREEN_WIDTH;
  *x = (f > 0.01) ? f : 0.01;
  f = (static_cast<float>(g_debug_screen->h)) / SCREEN_HEIGHT;
  *y = (f > 0.01) ? f : 0.01;
}

// Font: Apple Text
void DebuggerSetColorFG(ColorRef_t nRGB) { g_hConsoleBrushFG = nRGB; }

// Font: GDI/Console
void DebuggerSetColorBG(ColorRef_t nRGB, bool bTransparent) {
  (void)bTransparent;
  g_hConsoleBrushBG = nRGB;
}

void FillRect(const Rect_t* r, int Brush) {
  if (!r) {
    return;
  }
  int col_start = r->left / CONSOLE_FONT_WIDTH;
  int row_start = r->top / CONSOLE_FONT_HEIGHT;
  int col_end = r->right / CONSOLE_FONT_WIDTH;
  int row_end = r->bottom / CONSOLE_FONT_HEIGHT;
  for (int y = row_start; y < row_end; ++y) {
    for (int x = col_start; x < col_end; ++x) {
      if (x >= 0 && x < DEBUG_VIRTUAL_TEXT_WIDTH && y >= 0 &&
          y < DEBUG_VIRTUAL_TEXT_HEIGHT) {
        g_aDebuggerVirtualTextScreen[y][x] = ' ';
        g_aDebuggerVirtualTextScreenFG[y][x] = g_hConsoleBrushFG;
        g_aDebuggerVirtualTextScreenBG[y][x] = Brush;
      }
    }
  }
  if (g_debug_screen) {
    rectangle(g_debug_screen, r->left, r->top, r->right - r->left,
              r->bottom - r->top, Brush);
  }
}

void PrintGlyph(const int x, const int y, const char glyph) {
  char g = glyph;
  int ySrc = 64;

  if (glyph < 32) {
    // mouse text
    g -= 32;
    ySrc = 48;
  } else if ((glyph >= '@') && (glyph <= '_')) {
    g -= '@';
  } else if ((glyph >= ' ') && (glyph <= '?')) {
    g += 32 - ' ';
  } else if ((glyph >= '`') && (static_cast<uint8_t>(glyph) <= 127)) {
    g += 6 * 16 - '`';
  }

  int xSrc = (g & 0x0F) * CONSOLE_FONT_GRID_X;
  ySrc += ((g >> 4) * CONSOLE_FONT_GRID_Y);

  {
    int col = x / CONSOLE_FONT_WIDTH;
    int row = y / CONSOLE_FONT_HEIGHT;

    if (x > DISPLAY_DISASM_RIGHT) {
      col++;
    }

    if ((col >= 0) && (col < DEBUG_VIRTUAL_TEXT_WIDTH) && (row >= 0) &&
        (row < DEBUG_VIRTUAL_TEXT_HEIGHT)) {
      g_aDebuggerVirtualTextScreen[row][col] = glyph;
      g_aDebuggerVirtualTextScreenFG[row][col] = g_hConsoleBrushFG;
      g_aDebuggerVirtualTextScreenBG[row][col] = g_hConsoleBrushBG;
    }
  }

  uint32_t hBrush = g_hConsoleBrushFG;
  uint32_t hBgBrush = g_hConsoleBrushBG;
  if (g_debug_screen && g_hDebugCharset) {
    SOFTSTRECH_MONO(g_hDebugCharset, xSrc, ySrc, CONSOLE_FONT_WIDTH,
                    CONSOLE_FONT_HEIGHT, g_debug_screen, x, y,
                    CONSOLE_FONT_WIDTH, CONSOLE_FONT_HEIGHT);
  }
}

void DebuggerPrint(int x, int y, const char* pText) {
  if (!pText) return;
  const int nLeft = x;
  char c = 0;
  const char* p = pText;

  while ((c = *p)) {
    if (c == '\n') {
      x = nLeft;
      y += CONSOLE_FONT_HEIGHT;
      p++;
      continue;
    }
    c &= 0x7F;
    PrintGlyph(x, y, c);
    x += CONSOLE_FONT_WIDTH;
    p++;
  }
}

void DebuggerPrintColor(int x, int y, const conchar_t* pText) {
  int nLeft = x;
  conchar_t g = 0;
  const conchar_t* pSrc = pText;

  if (!pText) {
    return;
  }

  while ((g = (*pSrc))) {
    if (g == '\n') {
      x = nLeft;
      y += CONSOLE_FONT_HEIGHT;
      pSrc++;
      continue;
    }

    if (ConsoleColor_IsColorOrMouse(g)) {
      if (ConsoleColor_IsColor(g)) {
        DebuggerSetColorFG(ConsoleColor_GetColor(g));
      }
      g = ConsoleChar_GetChar(g);
    }

    PrintGlyph(x, y, static_cast<char>(g & _CONSOLE_COLOR_MASK));
    x += CONSOLE_FONT_WIDTH;
    pSrc++;
  }
}

auto can_draw_debugger() -> bool {
  return (g_state.mode == MODE_DEBUG) || (g_state.mode == MODE_STEPPING);
}

auto PrintText(const char* pText, Rect_t& rRect) -> int {
  if (!pText) return 0;
  int nLen = strlen(pText);

#if !DEBUG_FONT_NO_BACKGROUND_TEXT
  FillRect(&rRect, g_hConsoleBrushBG);
#endif

  DebuggerPrint(rRect.left, rRect.top, pText);
  return nLen;
}

void PrintTextColor(const conchar_t* pText, Rect_t& rRect) {
  if (!pText) return;
#if !DEBUG_FONT_NO_BACKGROUND_TEXT
  FillRect(&rRect, g_hConsoleBrushBG);
#endif

  DebuggerPrintColor(rRect.left, rRect.top, pText);
}

auto PrintTextCursorX(const char* pText, Rect_t& rRect) -> int {
  int nChars = 0;
  if (pText) {
    nChars = PrintText(pText, rRect);
    int nFontWidth = CONSOLE_FONT_WIDTH;
    rRect.left += (nFontWidth * nChars);
  }
  return nChars;
}

auto PrintTextCursorY(const char* pText, Rect_t& rRect) -> int {
  int nChars = PrintText(pText, rRect);
  rRect.top += CONSOLE_FONT_HEIGHT;
  rRect.bottom += CONSOLE_FONT_HEIGHT;
  return nChars;
}

// Font: GDI/Console
// Font: GDI/Console
void ConsoleDrawChar(int x, int y, char ch) { PrintGlyph(x, y, ch); }

// Font: GDI/Console
void ConsoleDrawText(int x, int y, const char* pText) {
  if (!pText) {
    return;
  }

  const char* pSrc = pText;
  int xCur = x;
  char c = 0;

  while (pSrc && (c = *pSrc)) {
    if (ConsoleColor_IsCharMeta(c)) {
      pSrc++;
      if (!*pSrc) {
        break;
      }

      if (ConsoleColor_IsCharColor(*pSrc)) {
        DebuggerSetColorFG(g_anConsoleColor[*pSrc - '0']);
      } else if (ConsoleColor_IsCharMeta(*pSrc))  // ``
      {
        ConsoleDrawChar(xCur, y, c);
        xCur += CONSOLE_FONT_WIDTH;
      }
      // else // `@  mouse text
    } else {
      ConsoleDrawChar(xCur, y, c);
      xCur += CONSOLE_FONT_WIDTH;
    }
    pSrc++;
  }
}

//===========================================================================
void DebuggerDrawChar(int x, int y, char ch) { PrintGlyph(x, y, ch); }

// Font: Apple Text
void DebuggerDrawText(int x, int y, const char* pText) {
  if (!pText) return;
  const char* pSrc = pText;
  int xCur = x;
  while (pSrc && *pSrc) {
    DebuggerDrawChar(xCur, y, *pSrc);
    xCur += APPLE_FONT_WIDTH;
    pSrc++;
  }
}

//===========================================================================
//===========================================================================
void DebuggerDrawCursor(int x, int y, char ch) { PrintGlyph(x, y, ch); }

//===========================================================================
void DrawConsoleCursor() {
  DebuggerSetColorFG(WHITE);
  DebuggerSetColorBG(BLACK, false);

  DebuggerDrawCursor(
      g_aWindowConfig[WINDOW_CONSOLE].left +
          (g_nConsoleInputChars + g_nConsolePromptLen) * APPLE_FONT_WIDTH,
      g_aWindowConfig[WINDOW_CONSOLE].bottom - APPLE_FONT_HEIGHT,
      g_sConsoleCursor[0]);
}

//===========================================================================
void DrawConsoleInput() {
  DebuggerSetColorFG(WHITE);
  DebuggerSetColorBG(BLACK, false);

  // Draw: Prompt + Input
  DebuggerDrawText(g_aWindowConfig[WINDOW_CONSOLE].left,
                   g_aWindowConfig[WINDOW_CONSOLE].bottom - APPLE_FONT_HEIGHT,
                   g_aConsoleInput);

  // Clear rest of line
  DebuggerSetColorFG(BLACK);
  VideoRect r{};
  r.x = g_aWindowConfig[WINDOW_CONSOLE].left +
        (g_nConsoleInputChars + g_nConsolePromptLen + 1) * APPLE_FONT_WIDTH;
  r.y = g_aWindowConfig[WINDOW_CONSOLE].bottom - APPLE_FONT_HEIGHT;
  r.w = g_aWindowConfig[WINDOW_CONSOLE].right - r.x;
  r.h = APPLE_FONT_HEIGHT;

  int col_start = r.x / APPLE_FONT_WIDTH;
  int row = r.y / APPLE_FONT_HEIGHT;
  int col_end = (r.x + r.w) / APPLE_FONT_WIDTH;
  for (int col = col_start; col < col_end; ++col) {
    if (col >= 0 && col < DEBUG_VIRTUAL_TEXT_WIDTH && row >= 0 &&
        row < DEBUG_VIRTUAL_TEXT_HEIGHT) {
      g_aDebuggerVirtualTextScreen[row][col] = ' ';
      g_aDebuggerVirtualTextScreenFG[row][col] = g_hConsoleBrushFG;
      g_aDebuggerVirtualTextScreenBG[row][col] = BLACK;
    }
  }

  if (g_debug_screen) {
    rectangle(g_debug_screen, r.x, r.y, r.w, r.h, BLACK);
  }
}

//===========================================================================
void DrawConsoleLine(const conchar_t* pText, int y_coord) {
  int x = g_aWindowConfig[WINDOW_CONSOLE].left;
  int y = g_aWindowConfig[WINDOW_CONSOLE].top + y_coord * APPLE_FONT_HEIGHT;

  const conchar_t* pSrc = pText;
  conchar_t g = 0;

  if (!pText) {
    // Clear line
    int col_start = x / APPLE_FONT_WIDTH;
    int row = y / APPLE_FONT_HEIGHT;
    int col_end = g_aWindowConfig[WINDOW_CONSOLE].right / APPLE_FONT_WIDTH;
    for (int col = col_start; col < col_end; ++col) {
      if (col >= 0 && col < DEBUG_VIRTUAL_TEXT_WIDTH && row >= 0 &&
          row < DEBUG_VIRTUAL_TEXT_HEIGHT) {
        g_aDebuggerVirtualTextScreen[row][col] = ' ';
        g_aDebuggerVirtualTextScreenFG[row][col] = g_hConsoleBrushFG;
        g_aDebuggerVirtualTextScreenBG[row][col] = BLACK;
      }
    }

    if (g_debug_screen) {
      rectangle(g_debug_screen, x, y, g_aWindowConfig[WINDOW_CONSOLE].right - x,
                APPLE_FONT_HEIGHT, BLACK);
    }
    return;
  }

  while (pSrc && (g = *pSrc)) {
    DebuggerSetColorFG(ConsoleColor_GetColor(g));
    DebuggerDrawChar(x, y, ConsoleChar_GetChar(g));
    x += APPLE_FONT_WIDTH;
    pSrc++;
  }
}

auto GetConsoleTopPixels(int y) -> int {
  return g_aWindowConfig[WINDOW_CONSOLE].top + (y * CONSOLE_FONT_HEIGHT);
}

void ColorizeFlags(bool bSet, int bg_default, int fg_default) {
  if (bSet) {
    DebuggerSetColorBG(DebuggerGetColor(BG_INFO_INVERSE));
    DebuggerSetColorFG(DebuggerGetColor(FG_INFO_INVERSE));
  } else {
    DebuggerSetColorBG(DebuggerGetColor(bg_default));
    DebuggerSetColorFG(DebuggerGetColor(fg_default));
  }
}

void DrawSubWindow_Info(Update_t bUpdate, int iWindow) {
  if (g_iWindowThis == WINDOW_CONSOLE) {
    return;
  }

  (void)bUpdate;
  (void)iWindow;
  DrawRegisters(0);
  DrawStack(10);
  DrawMemory(20, 0);
  DrawMemory(30, 1);
  DrawSoftSwitches(16);
}

auto ColorizeSpecialChar(char* sText, uint8_t nData, const MemoryView_e iView,
                         const int iAsciBackground, const int iTextForeground,
                         const int iHighBackground, const int iHighForeground,
                         const int iCtrlBackground, const int iCtrlForeground)
    -> char {
  (void)iView;
  (void)iAsciBackground;
  (void)iTextForeground;
  (void)iHighBackground;
  (void)iHighForeground;
  (void)iCtrlBackground;
  (void)iCtrlForeground;
  bool bCtrlBit = false;

  uint8_t nByte = (nData & 0x7F);
  if (nByte < 0x20) bCtrlBit = true;

  char nChar = static_cast<char>(nByte);
  if (bCtrlBit) nChar += '@';

  if (sText) sprintf(sText, "%c", nChar);
  return nChar;
}

auto FormatCharTxtHigh(const uint8_t b, bool* pWasHi_) -> char {
  if (pWasHi_) *pWasHi_ = (b > 0x7F);
  return b & 0x7F;
}

auto FormatCharTxtCtrl(const uint8_t b, bool* pWasCtrl_) -> char {
  if (pWasCtrl_) *pWasCtrl_ = (b < 0x20);
  return (b < 0x20) ? b + '@' : b;
}

auto FormatChar4Font(const uint8_t b, bool* pWasHi_, bool* pWasLo_) -> char {
  uint8_t b1 = FormatCharTxtHigh(b, pWasHi_);
  return FormatCharTxtCtrl(b1, pWasLo_);
}

const char* g_sConfigBranchIndicatorUp[NUM_DISASM_BRANCH_TYPES] = {" ", "^",
                                                                   "\x8B"};
const char* g_sConfigBranchIndicatorEqual[NUM_DISASM_BRANCH_TYPES] = {" ", "=",
                                                                      "\x88"};
const char* g_sConfigBranchIndicatorDown[NUM_DISASM_BRANCH_TYPES] = {" ", "v",
                                                                     "\x8A"};

auto FormatCharCopy(char* pDst, const char* pSrc, const int nLen) -> char* {
  for (int i = 0; i < nLen; i++) {
    *pDst++ = FormatCharTxtCtrl(*pSrc++, nullptr);
  }
  return pDst;
}

auto FormatCharCopyWrapped(char* pDst, uint16_t nStart, const int nLen)
    -> char* {
  for (int i = 0; i < nLen; i++) {
    *pDst++ =
        FormatCharTxtCtrl(mem[static_cast<uint16_t>(nStart + i)], nullptr);
  }
  return pDst;
}

void FormatOpcodeBytes(uint16_t nBaseAddress, DisasmLine_t& line_) {
  int nOpbyte = line_.nOpbyte;

  char* pDst = line_.sOpCodes;
  int nMaxOpBytes = nOpbyte;
  if (nMaxOpBytes > MAX_OPCODES) {
    nMaxOpBytes = MAX_OPCODES;
  }

  for (int iByte = 0; iByte < nMaxOpBytes; iByte++) {
    uint8_t nMem = *(mem + static_cast<uint16_t>(nBaseAddress + iByte));
    sprintf(pDst, "%02X", nMem);
    pDst += 2;

    if (g_bConfigDisasmOpcodeSpaces) {
      strcat(pDst, " ");
      pDst++;
    }
  }
}

void FormatNopcodeBytes(uint16_t nBaseAddress, DisasmLine_t& line_) {
  char* pDst = line_.sTarget;
  uint32_t nStartAddress = line_.pDisasmData->nStartAddress;
  uint32_t nEndAddress = line_.pDisasmData->nEndAddress;
  int nDisplayLen = nEndAddress - nBaseAddress + 1;
  int len = nDisplayLen;

  for (int iByte = 0; iByte < line_.nOpbyte;) {
    uint8_t nTarget8 = *(mem + static_cast<uint16_t>(nBaseAddress + iByte));
    uint16_t nTarget16 =
        *(mem + static_cast<uint16_t>(nBaseAddress + iByte)) |
        (*(mem + static_cast<uint16_t>(nBaseAddress + iByte + 1)) << 8);

    switch (line_.iNoptype) {
      case NOP_BYTE_1:
      case NOP_BYTE_2:
      case NOP_BYTE_4:
      case NOP_BYTE_8:
        sprintf(pDst, "%02X", nTarget8);
        pDst += 2;
        iByte++;
        if (line_.iNoptype == NOP_BYTE_1) {
          if (iByte < line_.nOpbyte) {
            *pDst++ = ',';
          }
        }
        break;
      case NOP_WORD_1:
      case NOP_WORD_2:
      case NOP_WORD_4:
        sprintf(pDst, "%04X", nTarget16);
        pDst += 4;
        iByte += 2;
        if (iByte < line_.nOpbyte) {
          *pDst++ = ',';
        }
        break;
      case NOP_ADDRESS:
        iByte += 2;
        break;
      case NOP_STRING_APPLESOFT:
        iByte = line_.nOpbyte;
        for (int i = 0; i < iByte; i++) {
          pDst[i] =
              static_cast<char>(mem[static_cast<uint16_t>(nBaseAddress + i)]);
        }
        pDst += iByte;
        *pDst = 0;
        break;
      case NOP_STRING_APPLE:
        iByte = line_.nOpbyte;

        if (len > (MAX_IMMEDIATE_LEN - 2)) {
          if (len > MAX_IMMEDIATE_LEN) {
            len = (MAX_IMMEDIATE_LEN - 3);
          }

          FormatCharCopyWrapped(pDst, static_cast<uint16_t>(nStartAddress),
                                len);
          pDst += len;

          if (nDisplayLen > len) {
            *pDst++ = '.';
            *pDst++ = '.';
            *pDst++ = '.';
          }
        } else {
          *pDst++ = '"';
          pDst = FormatCharCopyWrapped(
              pDst, static_cast<uint16_t>(nStartAddress), len);
          *pDst++ = '"';
        }

        *pDst = 0;
        break;
      default:
        iByte++;
        break;
    }
  }
}

void GetTargets_IgnoreDirectJSRJMP(const uint8_t iOpcode, int& nTargetPointer) {
  if (iOpcode == OPCODE_JSR || iOpcode == OPCODE_JMP_A) {
    nTargetPointer = NO_6502_TARGET;
  }
}

auto GetDisassemblyLine(uint16_t nBaseAddress, DisasmLine_t& line_) -> int {
  line_.Clear();

  int iOpcode = 0;
  int iOpmode = 0;
  int nOpbyte = 0;

  iOpcode =
      _6502_GetOpmodeOpbyte(nBaseAddress, iOpmode, nOpbyte, &line_.pDisasmData);
  const DisasmData_t* pData = line_.pDisasmData;

  line_.iOpcode = iOpcode;
  line_.iOpmode = iOpmode;
  line_.nOpbyte = nOpbyte;

  if (iOpmode == AM_M) {
    line_.bTargetImmediate = true;
  }

  if ((iOpmode >= AM_IZX) && (iOpmode <= AM_NA)) {
    line_.bTargetIndirect = true;
  }

  if ((iOpmode >= AM_IZX) && (iOpmode <= AM_NZY)) {
    line_.bTargetIndexed = true;
  }

  if (((iOpmode >= AM_A) && (iOpmode <= AM_ZY)) || line_.bTargetIndirect) {
    line_.bTargetValue = true;
  }

  if ((iOpmode == AM_AX) || (iOpmode == AM_ZX) || (iOpmode == AM_IZX) ||
      (iOpmode == AM_IAX)) {
    line_.bTargetX = true;
  }

  if ((iOpmode == AM_AY) || (iOpmode == AM_ZY) || (iOpmode == AM_NZY)) {
    line_.bTargetY = true;
  }

  unsigned int nMinBytesLen = (MAX_OPCODES * (2 + g_bConfigDisasmOpcodeSpaces));

  int bDisasmFormatFlags = 0;
  uint16_t nTarget = 0;

  if ((iOpmode != AM_IMPLIED) && (iOpmode != AM_1) && (iOpmode != AM_2) &&
      (iOpmode != AM_3)) {
    if (pData) {
      nTarget = pData->nTargetAddress;
    } else {
      nTarget = *(mem + static_cast<uint16_t>(nBaseAddress + 1)) |
                (*(mem + static_cast<uint16_t>(nBaseAddress + 2)) << 8);
      if (nOpbyte == 2) {
        nTarget &= 0xFF;
      }
    }

    if (iOpmode == AM_R) {
      line_.bTargetRelative = true;
      nTarget = nBaseAddress + 2 +
                static_cast<int>(static_cast<signed char>(nTarget));
      line_.nTarget = nTarget;
      sprintf(line_.sTargetValue, "%04X", nTarget & 0xFFFF);
      bDisasmFormatFlags |= DISASM_FORMAT_BRANCH;

      if (nTarget < nBaseAddress) {
        sprintf(line_.sBranch, "%s",
                g_sConfigBranchIndicatorUp[g_iConfigDisasmBranchType]);
      } else if (nTarget > nBaseAddress) {
        sprintf(line_.sBranch, "%s",
                g_sConfigBranchIndicatorDown[g_iConfigDisasmBranchType]);
      } else {
        sprintf(line_.sBranch, "%s",
                g_sConfigBranchIndicatorEqual[g_iConfigDisasmBranchType]);
      }
    }

    if ((iOpmode == AM_A) || (iOpmode == AM_Z) || (iOpmode == AM_AX) ||
        (iOpmode == AM_AY) || (iOpmode == AM_ZX) || (iOpmode == AM_ZY) ||
        (iOpmode == AM_R) || (iOpmode == AM_IZX) || (iOpmode == AM_IAX) ||
        (iOpmode == AM_NZY) || (iOpmode == AM_NZ) || (iOpmode == AM_NA)) {
      line_.nTarget = nTarget;
      const char* pTargetStr = nullptr;
      const char* pSymbol = FindSymbolFromAddress(nTarget);

      if (pData && (!pData->bSymbolLookup)) {
        pSymbol = nullptr;
      }

      if (pSymbol) {
        bDisasmFormatFlags |= DISASM_FORMAT_SYMBOL;
        pTargetStr = pSymbol;
      }

      if (!(bDisasmFormatFlags & DISASM_FORMAT_SYMBOL)) {
        pSymbol = FindSymbolFromAddress(nTarget - 1);
        if (pSymbol) {
          bDisasmFormatFlags |= DISASM_FORMAT_SYMBOL;
          bDisasmFormatFlags |= DISASM_FORMAT_OFFSET;
          pTargetStr = pSymbol;
          line_.nTargetOffset = +1;
        }
      }

      if (!(bDisasmFormatFlags & DISASM_FORMAT_SYMBOL) || pData) {
        pSymbol = FindSymbolFromAddress(nTarget + 1);
        if (pSymbol) {
          bDisasmFormatFlags |= DISASM_FORMAT_SYMBOL;
          bDisasmFormatFlags |= DISASM_FORMAT_OFFSET;
          pTargetStr = pSymbol;
          line_.nTargetOffset = -1;
        }
      }

      if (!(bDisasmFormatFlags & DISASM_FORMAT_SYMBOL)) {
        pTargetStr = FormatAddress(nTarget, (iOpmode != AM_R) ? nOpbyte : 3);
      }

      if (bDisasmFormatFlags & DISASM_FORMAT_OFFSET) {
        int nAbsTargetOffset = (line_.nTargetOffset > 0) ? line_.nTargetOffset
                                                         : -line_.nTargetOffset;
        sprintf(line_.sTargetOffset, "%d", nAbsTargetOffset);
      }
      sprintf(line_.sTarget, "%s", pTargetStr);

      int nTargetPartial = 0;
      int nTargetPartial2 = 0;
      int nTargetPointer = 0;
      uint16_t nTargetValue = 0;
      _6502_GetTargets(nBaseAddress, &nTargetPartial, &nTargetPartial2,
                       &nTargetPointer, nullptr);
      GetTargets_IgnoreDirectJSRJMP(iOpcode, nTargetPointer);

      if (nTargetPointer != NO_6502_TARGET) {
        bDisasmFormatFlags |= DISASM_FORMAT_TARGET_POINTER;
        nTargetValue = *(mem + nTargetPointer) |
                       (*(mem + ((nTargetPointer + 1) & 0xffff)) << 8);

        if (g_iConfigDisasmTargets & DISASM_TARGET_ADDR) {
          sprintf(line_.sTargetPointer, "%04X", nTargetPointer & 0xFFFF);
        }

        if (iOpcode != OPCODE_JMP_NA && iOpcode != OPCODE_JMP_IAX) {
          bDisasmFormatFlags |= DISASM_FORMAT_TARGET_VALUE;
          if (g_iConfigDisasmTargets & DISASM_TARGET_VAL) {
            sprintf(line_.sTargetValue, "%02X", nTargetValue & 0xFF);
          }

          bDisasmFormatFlags |= DISASM_FORMAT_CHAR;
          line_.nImmediate = static_cast<uint8_t>(nTargetValue);

          char _char = FormatCharTxtCtrl(
              FormatCharTxtHigh(line_.nImmediate, nullptr), nullptr);
          sprintf(line_.sImmediate, "%c", _char);
        }
      }
    } else if (iOpmode == AM_M) {
      sprintf(line_.sTarget, "%02X", static_cast<unsigned>(nTarget));
      if (iOpmode == AM_M) {
        bDisasmFormatFlags |= DISASM_FORMAT_CHAR;
        line_.nImmediate = static_cast<uint8_t>(nTarget);
        char _char = FormatCharTxtCtrl(
            FormatCharTxtHigh(line_.nImmediate, nullptr), nullptr);
        sprintf(line_.sImmediate, "%c", _char);
      }
    }
  }

  sprintf(line_.sAddress, "%04X", nBaseAddress);
  FormatOpcodeBytes(nBaseAddress, line_);

  if (pData) {
    line_.iNoptype = pData->eElementType;
    line_.iNopcode = pData->iDirective;
    strcpy(line_.sMnemonic, g_aAssemblerDirectives[line_.iNopcode].m_pMnemonic);
    FormatNopcodeBytes(nBaseAddress, line_);
  } else {
    strcpy(line_.sMnemonic, g_aOpcodes[line_.iOpcode].sMnemonic);
  }

  int nSpaces = strlen(line_.sOpCodes);
  while (nSpaces < static_cast<int>(nMinBytesLen)) {
    strcat(line_.sOpCodes, " ");
    nSpaces++;
  }

  return bDisasmFormatFlags;
}

auto FormatAddress(uint16_t nAddress, int nBytes) -> const char* {
  static char sBuffers[4][16];
  static int iBuf = 0;
  char* sAddress = sBuffers[iBuf];
  iBuf = (iBuf + 1) % 4;

  if (nBytes == 1) {
    sprintf(sAddress, "%02X", nAddress);
  } else {
    sprintf(sAddress, "%04X", nAddress);
  }
  return sAddress;
}

void InitDisasm() {
  for (int i = 0; i < NUM_FONTS; i++) {
    g_aFontConfig[i]._nFontWidthAvg = 7;
    g_aFontConfig[i]._nFontWidthMax = 7;
    g_aFontConfig[i]._nFontHeight = 8;
    g_aFontConfig[i]._nLineHeight = 8;
  }

  for (auto& i : g_aWindowConfig) {
    i.bSplit = false;
    i.left = 0;
    i.top = 0;
    i.right = 560;
    i.bottom = 384;
  }
  // Hardcoded layout for now, originally loaded from config
  g_aWindowConfig[WINDOW_CONSOLE].top = 300;
  g_nConsoleDisplayLines = (384 - 300) / 8;
  g_nDisasmWinHeight = 300 / 8;
  g_nDisplayMemoryLines = 8;

  ConsoleInputReset();
  WindowUpdateConsoleDisplayedSize();
}

void DrawWindowBottom(Update_t bUpdate, int iWindow) {
  (void)bUpdate;
  (void)iWindow;
}

//===========================================================================
void UpdateDisplay(Update_t bUpdate) {
  static int spDrawMutex = false;

  if (spDrawMutex) {
    return;
  }

  spDrawMutex = true;

  AllocateDebuggerMemDC();

  if (bUpdate & UPDATE_ALL) {
    memset(g_aDebuggerVirtualTextScreen, ' ',
           sizeof(g_aDebuggerVirtualTextScreen));
    for (int y = 0; y < DEBUG_VIRTUAL_TEXT_HEIGHT; ++y) {
      for (int x = 0; x < DEBUG_VIRTUAL_TEXT_WIDTH; ++x) {
        g_aDebuggerVirtualTextScreenFG[y][x] = WHITE;
        g_aDebuggerVirtualTextScreenBG[y][x] = BLACK;
      }
    }
    if (g_debug_screen) {
      memset(g_debug_screen->pixels, 0,
             static_cast<size_t>(g_debug_screen->pitch * g_debug_screen->h));
    }
  }

  switch (g_iWindowThis) {
    case WINDOW_CODE:
      DrawWindow_Code(bUpdate);
      break;

    case WINDOW_CONSOLE:
      DrawWindow_Console(bUpdate);
      break;

    case WINDOW_DATA:
      DrawWindow_Data(bUpdate);
      break;

    case WINDOW_IO:
      DrawWindow_IO(bUpdate);
      break;

    case WINDOW_SOURCE:
      DrawWindow_Source(bUpdate);
      break;

    case WINDOW_SYMBOLS:
      DrawWindow_Symbols(bUpdate);
      break;

    case WINDOW_ZEROPAGE:
      DrawWindow_ZeroPage(bUpdate);
      break;

    default:
      break;
  }

  if ((bUpdate & UPDATE_CONSOLE_DISPLAY) || (bUpdate & UPDATE_CONSOLE_INPUT)) {
    DrawSubWindow_Console(bUpdate);
  }

  if (g_debug_screen) {
    StretchBltMemToFrameDC();
  }

  spDrawMutex = false;
}

void debug_begin() {
  if (g_state.bDisableDebugger) {
    return;
  }
  // This is called every time the debugger is entered.
  g_state.mode = MODE_DEBUG;

  debug_initialize();
  AllocateDebuggerMemDC();

  g_state.mode = MODE_DEBUG;
  frame_refresh_status(DRAW_TITLE);

  UpdateDisplay(UPDATE_ALL);
}

void debug_destroy() {
  debug_end();

  for (int iTable = 0; iTable < NUM_SYMBOL_TABLES; iTable++) {
    _CmdSymbolsClear(static_cast<SymbolTable_Index_e>(iTable));
  }
}

void debug_end() {
  if (g_bProfiling) {
    ProfileFormat(true, PROFILE_FORMAT_TAB);
    ProfileSave();
  }

  if (g_hTraceFile) {
    fclose(g_hTraceFile);
    g_hTraceFile = nullptr;
  }

  extern std::vector<int> g_vMemorySearchResults;
  g_vMemorySearchResults.erase(g_vMemorySearchResults.begin(),
                               g_vMemorySearchResults.end());

  g_state.mode = MODE_RUNNING;

  ReleaseDebuggerMemDC();
}
