#include "Debugger_Display.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Debug.h"
#include "Debugger_Assembler.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Color.h"
#include "Debugger_Console.h"
#include "Debugger_Memory.h"
#include "Debugger_Types.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "charset40.xpm"  // US/default
#include "core/LinAppleCore.h"
#include "core/Util_Text.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/common/VideoSurface.h"

enum { DEBUG_FORCE_DISPLAY = 0 };

// Globals __________________________________________________________________

VideoSurface_t* g_debug_screen = nullptr;
VideoSurface_t* g_debug_charset = nullptr;

ColorRef_t g_console_brush_fg = WHITE;
ColorRef_t g_console_brush_bg = BLACK;

FontConfig_t g_font_config[NUM_FONTS];
char g_debugger_virtual_text_screen[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                   [DEBUG_VIRTUAL_TEXT_WIDTH];
ColorRef_t g_debugger_virtual_text_screen_fg[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                            [DEBUG_VIRTUAL_TEXT_WIDTH];
ColorRef_t g_debugger_virtual_text_screen_bg[DEBUG_VIRTUAL_TEXT_HEIGHT]
                                            [DEBUG_VIRTUAL_TEXT_WIDTH];

extern int g_window_last;
extern int g_window_this;
extern WindowSplit_t g_window_config[NUM_WINDOWS];

extern int g_disasm_win_height;
extern int g_console_display_lines;
int g_display_memory_lines = 8;
VideoScannerDisplayInfo_t g_video_scanner_display_info;

// Prototypes _______________________________________________________________

extern void DisasmInit();
extern auto _CmdSymbolsClear(SymbolTable_Index_e eSymbolTable) -> Update_t;
extern void frame_refresh_status(int);

extern void DrawSubWindow_Symbols(Update_t bUpdate);
extern void DrawSubWindow_ZeroPage(Update_t bUpdate);
extern void DrawSubWindow_Source(Update_t bUpdate);

void DrawSubWindow_IO(Update_t) {}

// Implementation ___________________________________________________________

#define SOFTSTRECH(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, DST_W, \
                   DST_H)                                                     \
  {                                                                           \
    VideoRect_t srcrect = {SRC_X, SRC_Y, SRC_W, SRC_H};                       \
    VideoRect_t dstrect = {DST_X, DST_Y, DST_W, DST_H};                       \
    video_soft_stretch(SRC, &srcrect, DST, &dstrect);                         \
  }

#define SOFTSTRECH_MONO(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y,   \
                        DST_W, DST_H)                                         \
  {                                                                           \
    VideoRect_t srcrect = {SRC_X, SRC_Y, SRC_W, SRC_H};                       \
    VideoRect_t dstrect = {DST_X, DST_Y, DST_W, DST_H};                       \
    video_soft_stretch_mono8(SRC, &srcrect, DST, &dstrect, hBrush, hBgBrush); \
  }

//===========================================================================

constexpr float MIN_VIEWPORT_SCALE = 0.01f;

void AllocateDebuggerMemDC() {
  if (!g_debug_screen) {
    g_debug_screen = video_create_surface(DISPLAY_WIDTH, DISPLAY_HEIGHT, 1);
    if (g_debug_screen) {
      VideoColor_t* pal = video_get_output_palette();
      if (pal) {
        memcpy(g_debug_screen->palette.data(), pal,
               VIDEO_PALETTE_SIZE * sizeof(VideoColor_t));
      }
    }
    g_debug_charset = video_load_xpm(charset40_xpm);
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
  *x = (f > MIN_VIEWPORT_SCALE) ? f : MIN_VIEWPORT_SCALE;
  f = (static_cast<float>(g_debug_screen->h)) / SCREEN_HEIGHT;
  *y = (f > MIN_VIEWPORT_SCALE) ? f : MIN_VIEWPORT_SCALE;
}

// Font: Apple Text
void DebuggerSetColorFG(ColorRef_t nRGB) { g_console_brush_fg = nRGB; }

// Font: GDI/Console
void DebuggerSetColorBG(ColorRef_t nRGB, bool bTransparent) {
  (void)bTransparent;
  g_console_brush_bg = nRGB;
}

void FillRect(const Rect_t* r, int Brush) {
  if (!r) {
    return;
  }
  int col_start = 0;
  int col_end = 0;
  int row_start = 0;
  int row_end = 0;

  if (r->top >= g_window_config[WINDOW_CONSOLE].top) {
    col_start = r->left / APPLE_FONT_WIDTH;
    col_end = (r->right + APPLE_FONT_WIDTH - 1) / APPLE_FONT_WIDTH;
    row_start =
        g_window_config[WINDOW_CONSOLE].top / CONSOLE_FONT_HEIGHT +
        (r->top - g_window_config[WINDOW_CONSOLE].top) / APPLE_FONT_HEIGHT;
    row_end = g_window_config[WINDOW_CONSOLE].top / CONSOLE_FONT_HEIGHT +
              (r->bottom - g_window_config[WINDOW_CONSOLE].top +
               APPLE_FONT_HEIGHT - 1) /
                  APPLE_FONT_HEIGHT;
  } else {
    col_start = r->left / CONSOLE_FONT_WIDTH;
    col_end = (r->right + CONSOLE_FONT_WIDTH - 1) / CONSOLE_FONT_WIDTH;
    row_start = r->top / CONSOLE_FONT_HEIGHT;
    row_end = (r->bottom + CONSOLE_FONT_HEIGHT - 1) / CONSOLE_FONT_HEIGHT;
  }

  for (int y = row_start; y < row_end; ++y) {
    for (int x = col_start; x < col_end; ++x) {
      if (x >= 0 && x < DEBUG_VIRTUAL_TEXT_WIDTH && y >= 0 &&
          y < DEBUG_VIRTUAL_TEXT_HEIGHT) {
        g_debugger_virtual_text_screen[y][x] = ' ';
        g_debugger_virtual_text_screen_fg[y][x] = g_console_brush_fg;
        g_debugger_virtual_text_screen_bg[y][x] = Brush;
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

    if (y >= g_window_config[WINDOW_CONSOLE].top) {
      col = x / APPLE_FONT_WIDTH;
      row = g_window_config[WINDOW_CONSOLE].top / CONSOLE_FONT_HEIGHT +
            (y - g_window_config[WINDOW_CONSOLE].top) / APPLE_FONT_HEIGHT;
    }

    if ((col >= 0) && (col < DEBUG_VIRTUAL_TEXT_WIDTH) && (row >= 0) &&
        (row < DEBUG_VIRTUAL_TEXT_HEIGHT)) {
      g_debugger_virtual_text_screen[row][col] = glyph;
      g_debugger_virtual_text_screen_fg[row][col] = g_console_brush_fg;
      g_debugger_virtual_text_screen_bg[row][col] = g_console_brush_bg;
    }
  }

  uint32_t hBrush = g_console_brush_fg;
  uint32_t hBgBrush = g_console_brush_bg;
  if (g_debug_screen && g_debug_charset) {
    SOFTSTRECH_MONO(g_debug_charset, xSrc, ySrc, CONSOLE_FONT_WIDTH,
                    CONSOLE_FONT_HEIGHT, g_debug_screen, x, y,
                    CONSOLE_FONT_WIDTH, CONSOLE_FONT_HEIGHT);
  }
}

void DebuggerPrint(int x, int y, const char* text) {
  if (!text) return;
  const int nLeft = x;
  char c = 0;
  const char* p = text;

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

void DebuggerPrintColor(int x, int y, const conchar_t* text) {
  int nLeft = x;
  conchar_t g = 0;
  const conchar_t* src_ptr = text;

  if (!text) {
    return;
  }

  while ((g = (*src_ptr))) {
    if (g == '\n') {
      x = nLeft;
      y += CONSOLE_FONT_HEIGHT;
      src_ptr++;
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
    src_ptr++;
  }
}

auto can_draw_debugger() -> bool {
  return (g_state.mode == MODE_DEBUG) || (g_state.mode == MODE_STEPPING);
}

auto PrintText(const char* text, Rect_t& rRect) -> int {
  if (!text) return 0;
  int nLen = static_cast<int>(strlen(text));

#if !DEBUG_FONT_NO_BACKGROUND_TEXT
  if (g_debug_screen) {
    Rect_t textRect = rRect;
    textRect.right = textRect.left + (nLen * CONSOLE_FONT_WIDTH);
    rectangle(g_debug_screen, textRect.left, textRect.top,
              textRect.right - textRect.left, textRect.bottom - textRect.top,
              g_console_brush_bg);
  }
#endif

  DebuggerPrint(rRect.left, rRect.top, text);
  return nLen;
}

void PrintTextColor(const conchar_t* text, Rect_t& rRect) {
  if (!text) return;
#if !DEBUG_FONT_NO_BACKGROUND_TEXT
  if (g_debug_screen) {
    int nLen = 0;
    const conchar_t* p = text;
    while (*p) {
      if (!ConsoleColor_IsColorOrMouse(*p) && *p != '\n') {
        nLen++;
      }
      p++;
    }
    Rect_t textRect = rRect;
    textRect.right = textRect.left + (nLen * CONSOLE_FONT_WIDTH);
    rectangle(g_debug_screen, textRect.left, textRect.top,
              textRect.right - textRect.left, textRect.bottom - textRect.top,
              g_console_brush_bg);
  }
#endif

  DebuggerPrintColor(rRect.left, rRect.top, text);
}

auto PrintTextCursorX(const char* text, Rect_t& rRect) -> int {
  int nChars = 0;
  if (text) {
    nChars = PrintText(text, rRect);
    int nFontWidth = CONSOLE_FONT_WIDTH;
    rRect.left += (nFontWidth * nChars);
  }
  return nChars;
}

auto PrintTextCursorY(const char* text, Rect_t& rRect) -> int {
  int nChars = PrintText(text, rRect);
  rRect.top += CONSOLE_FONT_HEIGHT;
  rRect.bottom += CONSOLE_FONT_HEIGHT;
  return nChars;
}

// Font: GDI/Console
// Font: GDI/Console
void ConsoleDrawChar(int x, int y, char ch) { PrintGlyph(x, y, ch); }

// Font: GDI/Console
void ConsoleDrawText(int x, int y, const char* text) {
  if (!text) {
    return;
  }

  const char* src_ptr = text;
  int xCur = x;
  char c = 0;

  while (src_ptr && (c = *src_ptr)) {
    if (ConsoleColor_IsCharMeta(c)) {
      src_ptr++;
      if (!*src_ptr) {
        break;
      }

      if (ConsoleColor_IsCharColor(*src_ptr)) {
        DebuggerSetColorFG(g_console_color[*src_ptr - '0']);
      } else if (ConsoleColor_IsCharMeta(*src_ptr))  // ``
      {
        ConsoleDrawChar(xCur, y, c);
        xCur += CONSOLE_FONT_WIDTH;
      }
      // else // `@  mouse text
    } else {
      ConsoleDrawChar(xCur, y, c);
      xCur += CONSOLE_FONT_WIDTH;
    }
    src_ptr++;
  }
}

//===========================================================================
void DebuggerDrawChar(int x, int y, char ch) { PrintGlyph(x, y, ch); }

// Font: Apple Text
void DebuggerDrawText(int x, int y, const char* text) {
  if (!text) return;
  const char* src_ptr = text;
  int xCur = x;
  while (src_ptr && *src_ptr) {
    DebuggerDrawChar(xCur, y, *src_ptr);
    xCur += APPLE_FONT_WIDTH;
    src_ptr++;
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
      g_window_config[WINDOW_CONSOLE].left +
          (g_console_input_chars + g_console_prompt_len) * APPLE_FONT_WIDTH,
      g_window_config[WINDOW_CONSOLE].bottom - APPLE_FONT_HEIGHT,
      g_console_cursor[0]);
}

//===========================================================================
void DrawConsoleInput() {
  DebuggerSetColorFG(WHITE);
  DebuggerSetColorBG(BLACK, false);

  // Draw: Prompt + Input
  DebuggerDrawText(g_window_config[WINDOW_CONSOLE].left,
                   g_window_config[WINDOW_CONSOLE].bottom - APPLE_FONT_HEIGHT,
                   g_console_input);

  // Draw cursor right after input text
  DebuggerDrawCursor(
      g_window_config[WINDOW_CONSOLE].left +
          (g_console_input_chars + g_console_prompt_len) * APPLE_FONT_WIDTH,
      g_window_config[WINDOW_CONSOLE].bottom - APPLE_FONT_HEIGHT,
      g_console_cursor[0]);

  // Clear rest of line
  DebuggerSetColorFG(WHITE);
  VideoRect_t r{};
  r.x = g_window_config[WINDOW_CONSOLE].left +
        (g_console_input_chars + g_console_prompt_len + 1) * APPLE_FONT_WIDTH;
  r.y = g_window_config[WINDOW_CONSOLE].bottom - APPLE_FONT_HEIGHT;
  r.w = g_window_config[WINDOW_CONSOLE].right - r.x;
  r.h = APPLE_FONT_HEIGHT;

  int col_start = r.x / APPLE_FONT_WIDTH;
  int row = g_window_config[WINDOW_CONSOLE].top / CONSOLE_FONT_HEIGHT +
            (r.y - g_window_config[WINDOW_CONSOLE].top) / APPLE_FONT_HEIGHT;
  for (int col = col_start; col < DEBUG_VIRTUAL_TEXT_WIDTH; ++col) {
    if (col >= 0 && col < DEBUG_VIRTUAL_TEXT_WIDTH && row >= 0 &&
        row < DEBUG_VIRTUAL_TEXT_HEIGHT) {
      g_debugger_virtual_text_screen[row][col] = ' ';
      g_debugger_virtual_text_screen_fg[row][col] = WHITE;
      g_debugger_virtual_text_screen_bg[row][col] = BLACK;
    }
  }

  if (g_debug_screen) {
    rectangle(g_debug_screen, r.x, r.y, r.w, r.h, BLACK);
  }
}

//===========================================================================
void DrawConsoleLine(const conchar_t* text, int y_coord) {
  int x = g_window_config[WINDOW_CONSOLE].left;
  int y = g_window_config[WINDOW_CONSOLE].top + y_coord * APPLE_FONT_HEIGHT;

  const conchar_t* src_ptr = text;
  conchar_t g = 0;

  if (!text) {
    // Clear line
    int col_start = x / APPLE_FONT_WIDTH;
    int row =
        g_window_config[WINDOW_CONSOLE].top / CONSOLE_FONT_HEIGHT + y_coord;
    for (int col = col_start; col < DEBUG_VIRTUAL_TEXT_WIDTH; ++col) {
      if (col >= 0 && col < DEBUG_VIRTUAL_TEXT_WIDTH && row >= 0 &&
          row < DEBUG_VIRTUAL_TEXT_HEIGHT) {
        g_debugger_virtual_text_screen[row][col] = ' ';
        g_debugger_virtual_text_screen_fg[row][col] = g_console_brush_fg;
        g_debugger_virtual_text_screen_bg[row][col] = BLACK;
      }
    }

    if (g_debug_screen) {
      rectangle(g_debug_screen, x, y, g_window_config[WINDOW_CONSOLE].right - x,
                APPLE_FONT_HEIGHT, BLACK);
    }
    return;
  }

  while (src_ptr && (g = *src_ptr)) {
    DebuggerSetColorFG(ConsoleColor_GetColor(g));
    DebuggerDrawChar(x, y, ConsoleChar_GetChar(g));
    x += APPLE_FONT_WIDTH;
    src_ptr++;
  }
}

auto GetConsoleTopPixels(int y) -> int {
  return g_window_config[WINDOW_CONSOLE].top + (y * CONSOLE_FONT_HEIGHT);
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
  if (g_window_this == WINDOW_CONSOLE) {
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

  if (sText) snprintf(sText, 2, "%c", nChar);
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

const char* g_config_branch_indicator_up[NUM_DISASM_BRANCH_TYPES] = {" ", "^",
                                                                     "\x8B"};
const char* g_config_branch_indicator_equal[NUM_DISASM_BRANCH_TYPES] = {
    " ", "=", "\x88"};
const char* g_config_branch_indicator_down[NUM_DISASM_BRANCH_TYPES] = {" ", "v",
                                                                       "\x8A"};

auto FormatCharCopy(char* pDst, const char* src_ptr, const int nLen) -> char* {
  for (int i = 0; i < nLen; i++) {
    *pDst++ = FormatCharTxtCtrl(*src_ptr++, nullptr);
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

  for (int byte = 0; byte < nMaxOpBytes; byte++) {
    uint8_t nMem = *(mem + static_cast<uint16_t>(nBaseAddress + byte));
    snprintf(pDst, 3, "%02X", nMem);
    pDst += 2;

    if (g_config_disasm_opcode_spaces) {
      *pDst = ' ';
      *(pDst + 1) = '\0';
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

  for (int byte = 0; byte < line_.nOpbyte;) {
    uint8_t nTarget8 = *(mem + static_cast<uint16_t>(nBaseAddress + byte));
    uint16_t nTarget16 =
        *(mem + static_cast<uint16_t>(nBaseAddress + byte)) |
        (*(mem + static_cast<uint16_t>(nBaseAddress + byte + 1)) << 8);

    switch (line_.iNoptype) {
      case NOP_BYTE_1:
      case NOP_BYTE_2:
      case NOP_BYTE_4:
      case NOP_BYTE_8:
        snprintf(pDst, 3, "%02X", nTarget8);
        pDst += 2;
        byte++;
        if (line_.iNoptype == NOP_BYTE_1) {
          if (byte < line_.nOpbyte) {
            *pDst++ = ',';
          }
        }
        break;
      case NOP_WORD_1:
      case NOP_WORD_2:
      case NOP_WORD_4:
        snprintf(pDst, 5, "%04X", nTarget16);
        pDst += 4;
        byte += 2;
        if (byte < line_.nOpbyte) {
          *pDst++ = ',';
        }
        break;
      case NOP_ADDRESS:
        byte += 2;
        break;
      case NOP_STRING_APPLESOFT:
        byte = line_.nOpbyte;
        for (int i = 0; i < byte; i++) {
          pDst[i] =
              static_cast<char>(mem[static_cast<uint16_t>(nBaseAddress + i)]);
        }
        pDst += byte;
        *pDst = 0;
        break;
      case NOP_STRING_APPLE:
        byte = line_.nOpbyte;

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
        byte++;
        break;
    }
  }
}

void GetTargets_IgnoreDirectJSRJMP(const uint8_t opcode, int& nTargetPointer) {
  if (opcode == OPCODE_JSR || opcode == OPCODE_JMP_A) {
    nTargetPointer = NO_6502_TARGET;
  }
}

auto GetDisassemblyLine(uint16_t nBaseAddress, DisasmLine_t& line_) -> int {
  line_.Clear();

  int opcode = 0;
  int iOpmode = 0;
  int nOpbyte = 0;

  opcode =
      _6502_GetOpmodeOpbyte(nBaseAddress, iOpmode, nOpbyte, &line_.pDisasmData);
  const DisasmData_t* data = line_.pDisasmData;

  line_.opcode = opcode;
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

  unsigned int nMinBytesLen =
      (MAX_OPCODES * (2 + g_config_disasm_opcode_spaces));

  int bDisasmFormatFlags = 0;
  uint16_t nTarget = 0;

  if ((iOpmode != AM_IMPLIED) && (iOpmode != AM_1) && (iOpmode != AM_2) &&
      (iOpmode != AM_3)) {
    if (data) {
      nTarget = data->nTargetAddress;
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
      snprintf(line_.sTargetValue, sizeof(line_.sTargetValue), "%04X",
               nTarget & 0xFFFF);
      bDisasmFormatFlags |= DISASM_FORMAT_BRANCH;

      if (nTarget < nBaseAddress) {
        snprintf(line_.sBranch, sizeof(line_.sBranch), "%s",
                 g_config_branch_indicator_up[g_config_disasm_branch_type]);
      } else if (nTarget > nBaseAddress) {
        snprintf(line_.sBranch, sizeof(line_.sBranch), "%s",
                 g_config_branch_indicator_down[g_config_disasm_branch_type]);
      } else {
        snprintf(line_.sBranch, sizeof(line_.sBranch), "%s",
                 g_config_branch_indicator_equal[g_config_disasm_branch_type]);
      }
    }

    if ((iOpmode == AM_A) || (iOpmode == AM_Z) || (iOpmode == AM_AX) ||
        (iOpmode == AM_AY) || (iOpmode == AM_ZX) || (iOpmode == AM_ZY) ||
        (iOpmode == AM_R) || (iOpmode == AM_IZX) || (iOpmode == AM_IAX) ||
        (iOpmode == AM_NZY) || (iOpmode == AM_NZ) || (iOpmode == AM_NA)) {
      line_.nTarget = nTarget;
      const char* pTargetStr = nullptr;
      const char* pSymbol = FindSymbolFromAddress(nTarget);

      if (data && (!data->bSymbolLookup)) {
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

      if (!(bDisasmFormatFlags & DISASM_FORMAT_SYMBOL) || data) {
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
        snprintf(line_.sTargetOffset, sizeof(line_.sTargetOffset), "%d",
                 nAbsTargetOffset);
      }
      snprintf(line_.sTarget, sizeof(line_.sTarget), "%s", pTargetStr);

      int nTargetPartial = 0;
      int nTargetPartial2 = 0;
      int nTargetPointer = 0;
      uint16_t nTargetValue = 0;
      _6502_GetTargets(nBaseAddress, &nTargetPartial, &nTargetPartial2,
                       &nTargetPointer, nullptr);
      GetTargets_IgnoreDirectJSRJMP(opcode, nTargetPointer);

      if (nTargetPointer != NO_6502_TARGET) {
        bDisasmFormatFlags |= DISASM_FORMAT_TARGET_POINTER;
        nTargetValue = *(mem + nTargetPointer) |
                       (*(mem + ((nTargetPointer + 1) & 0xffff)) << 8);

        if (g_config_disasm_targets & DISASM_TARGET_ADDR) {
          snprintf(line_.sTargetPointer, sizeof(line_.sTargetPointer), "%04X",
                   nTargetPointer & 0xFFFF);
        }

        if (opcode != OPCODE_JMP_NA && opcode != OPCODE_JMP_IAX) {
          bDisasmFormatFlags |= DISASM_FORMAT_TARGET_VALUE;
          if (g_config_disasm_targets & DISASM_TARGET_VAL) {
            snprintf(line_.sTargetValue, sizeof(line_.sTargetValue), "%02X",
                     nTargetValue & 0xFF);
          }

          bDisasmFormatFlags |= DISASM_FORMAT_CHAR;
          line_.nImmediate = static_cast<uint8_t>(nTargetValue);

          char _char = FormatCharTxtCtrl(
              FormatCharTxtHigh(line_.nImmediate, nullptr), nullptr);
          snprintf(line_.sImmediate, sizeof(line_.sImmediate), "%c", _char);
        }
      }
    } else if (iOpmode == AM_M) {
      snprintf(line_.sTarget, sizeof(line_.sTarget), "%02X",
               static_cast<unsigned>(nTarget));
      if (iOpmode == AM_M) {
        bDisasmFormatFlags |= DISASM_FORMAT_CHAR;
        line_.nImmediate = static_cast<uint8_t>(nTarget);
        char _char = FormatCharTxtCtrl(
            FormatCharTxtHigh(line_.nImmediate, nullptr), nullptr);
        snprintf(line_.sImmediate, sizeof(line_.sImmediate), "%c", _char);
      }
    }
  }

  snprintf(line_.sAddress, sizeof(line_.sAddress), "%04X", nBaseAddress);
  FormatOpcodeBytes(nBaseAddress, line_);

  if (data) {
    line_.iNoptype = data->eElementType;
    line_.iNopcode = data->iDirective;
    util_safe_strcpy(line_.sMnemonic,
                     g_assembler_directives[line_.iNopcode].mnemonic,
                     sizeof(line_.sMnemonic));
    FormatNopcodeBytes(nBaseAddress, line_);
  } else {
    util_safe_strcpy(line_.sMnemonic, g_opcodes[line_.opcode].sMnemonic,
                     sizeof(line_.sMnemonic));
  }

  int nSpaces = strlen(line_.sOpCodes);
  while (nSpaces < static_cast<int>(nMinBytesLen)) {
    strncat(line_.sOpCodes, " ",
            sizeof(line_.sOpCodes) - strlen(line_.sOpCodes) - 1);
    nSpaces++;
  }

  return bDisasmFormatFlags;
}

auto FormatAddress(uint16_t address, int nBytes) -> const char* {
  static char sBuffers[4][16];
  static int iBuf = 0;
  char* sAddress = sBuffers[iBuf];
  iBuf = (iBuf + 1) % 4;

  if (nBytes == 1) {
    snprintf(sAddress, sizeof(sBuffers[0]), "%02X", address);
  } else {
    snprintf(sAddress, sizeof(sBuffers[0]), "%04X", address);
  }
  return sAddress;
}

constexpr int CONSOLE_WINDOW_TOP = 256;
constexpr int DEFAULT_DISPLAY_MEMORY_LINES = 8;

void InitDisasm() {
  for (int i = 0; i < NUM_FONTS; i++) {
    g_font_config[i]._nFontWidthAvg = CONSOLE_FONT_WIDTH;
    g_font_config[i]._nFontWidthMax = CONSOLE_FONT_WIDTH;
    g_font_config[i]._nFontHeight = CONSOLE_FONT_HEIGHT;
    g_font_config[i]._nLineHeight = CONSOLE_FONT_HEIGHT;
  }

  for (auto& i : g_window_config) {
    i.bSplit = false;
    i.left = 0;
    i.top = 0;
    i.right = DISPLAY_WIDTH;
    i.bottom = DISPLAY_HEIGHT;
  }
  // Hardcoded layout for now, originally loaded from config
  g_window_config[WINDOW_CONSOLE].top = CONSOLE_WINDOW_TOP;
  g_console_display_lines =
      (DISPLAY_HEIGHT - CONSOLE_WINDOW_TOP) / CONSOLE_FONT_HEIGHT;
  g_disasm_win_height = CONSOLE_WINDOW_TOP / CONSOLE_FONT_HEIGHT;
  g_display_memory_lines = DEFAULT_DISPLAY_MEMORY_LINES;

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
    memset(g_debugger_virtual_text_screen, ' ',
           sizeof(g_debugger_virtual_text_screen));
    for (int y = 0; y < DEBUG_VIRTUAL_TEXT_HEIGHT; ++y) {
      for (int x = 0; x < DEBUG_VIRTUAL_TEXT_WIDTH; ++x) {
        g_debugger_virtual_text_screen_fg[y][x] = WHITE;
        g_debugger_virtual_text_screen_bg[y][x] = BLACK;
      }
    }
    if (g_debug_screen) {
      memset(g_debug_screen->pixels, 0,
             static_cast<size_t>(g_debug_screen->pitch * g_debug_screen->h));
    }
  }

  switch (g_window_this) {
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
    stretch_blt_mem_to_frame_dc();
  }

  spDrawMutex = false;
}

void debug_begin() {
  if (g_state.disable_debugger) {
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
  if (g_profiling) {
    ProfileFormat(true, PROFILE_FORMAT_TAB);
    ProfileSave();
  }

  g_trace_file.reset();

  extern std::vector<int> g_memory_search_results;
  g_memory_search_results.erase(g_memory_search_results.begin(),
                                g_memory_search_results.end());

  g_state.mode = MODE_RUNNING;

  ReleaseDebuggerMemDC();
}
