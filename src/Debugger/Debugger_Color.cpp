// SPDX-License-Identifier: GPL-2.0-only

#include "Debugger_Color.h"

#include <cstdint>
#include <cstdio>

#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Types.h"
#include "apple2/Video.h"

// Color ______________________________________________________________________

int g_color_scheme = SCHEME_COLOR;

int g_color_palette[NUM_PALETTE] = {
    BLACK,
    // NOTE: See SetupColorRamp() if you want to programmatically set/change
    RED, RED, RED, DARK_RED, DARK_RED, DARK_RED, DARK_RED,
    DARK_RED,  // 001 // Red
    GREEN, GREEN, MONOCHROME_GREEN, MONOCHROME_GREEN, DARK_GREEN, DARKER_GREEN,
    DARKER_GREEN, DARKEST_GREEN,  // 010 // Green
    YELLOW, YELLOW, YELLOW, DARK_YELLOW, DARK_YELLOW, DARKER_YELLOW,
    DARKER_YELLOW, DARKEST_YELLOW,  // 011 // Yellow
    BLUE, BLUE, BLUE, BLUE, DARK_BLUE, DARK_BLUE, DARKER_BLUE,
    DARKER_BLUE,  // 100 // Blue
    MAGENTA, MAGENTA, MAGENTA, MAGENTA, HGR_MAGENTA, HGR_MAGENTA, HGR_MAGENTA,
    HGR_MAGENTA,  // 101 // Magenta
    CYAN, CYAN, CYAN, CYAN, DARK_CYAN, DARK_CYAN, DARKER_CYAN,
    DARKEST_CYAN,  // 110 // Cyan
    WHITE, LIGHTEST_GRAY, LIGHT_GRAY, MEDIUM_GRAY, HGR_GREY1, HGR_GREY1,
    HGR_GREY1, HGR_GREY1,  // 111 // White/Gray

    // Custom Colors
    LIGHT_SKY_BLUE,   // Light  Sky Blue // Used for console FG
    DARKER_SKY_BLUE,  // Darker Sky Blue
    DEEP_SKY_BLUE,    // Deep   Sky Blue
    ORANGE,           // Orange (Full)
    HALF_ORANGE,      // Orange (Half)
    0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0};

// Index into "Palette" of colors
int g_color_index[NUM_DEBUG_COLORS] = {
    K0,
    W8,  // BG_CONSOLE_OUTPUT   FG_CONSOLE_OUTPUT (W8)
    B2,
    COLOR_CUSTOM_01,  // BG_CONSOLE_INPUT    FG_CONSOLE_INPUT (W8)

    B2,  // BG_DISASM_1
    B3,  // BG_DISASM_2

    R8,
    W8,  // BG_DISASM_BP_S_C    FG_DISASM_BP_S_C
    R6,
    W5,  // BG_DISASM_BP_0_C    FG_DISASM_BP_0_C

    R7,  // FG_DISASM_BP_S_X    // Y8 lookes better on info Cyan // R6
    W5,  // FG_DISASM_BP_0_X

    W8,
    K0,  // BG_DISASM_C         FG_DISASM_C
    Y8,
    K0,  // BG_DISASM_PC_C      FG_DISASM_PC_C
    Y4,
    W8,  // BG_DISASM_PC_X      FG_DISASM_PC_X

    C4,
    W8,  // BG_DISASM_BOOKMARK  FG_DISASM_BOOKMARK

    W8,               //                     FG_DISASM_ADDRESS
    G192,             //                     FG_DISASM_OPERATOR
    Y8,               //                     FG_DISASM_OPCODE
    W8,               //                     FG_DISASM_MNEMONIC
    M8,               //                     FG_DISASM_DIRECTIVE
    COLOR_CUSTOM_04,  //                     FG_DISASM_TARGET (or W8)
    G8,               //                     FG_DISASM_SYMBOL
    C8,               //                     FG_DISASM_CHAR
    G8,               //                     FG_DISASM_BRANCH

    C3,               // BG_INFO (C4, C2 too dark)
    C3,               // BG_INFO_WATCH
    C3,               // BG_INFO_ZEROPAGE
    W8,               //                     FG_INFO_TITLE (or W8)
    Y7,               //                     FG_INFO_BULLET (W8)
    G192,             //                     FG_INFO_OPERATOR
    COLOR_CUSTOM_04,  //                     FG_INFO_ADDRESS (was Y8)
    Y8,               //                     FG_INFO_OPCODE
    COLOR_CUSTOM_01,  //                     FG_INFO_REG (was orange)

    W8,
    C3,  // BG_INFO_INVERSE     FG_INFO_INVERSE
    C5,  // BG_INFO_CHAR
    W8,  //                     FG_INFO_CHAR_HI
    Y8,  //                     FG_INFO_CHAR_LO

    COLOR_CUSTOM_04,  // BG_INFO_IO_BYTE
    COLOR_CUSTOM_04,  //                     FG_INFO_IO_BYTE

    C1,  // BG_DATA_1 // 2.6.2.24 Changed: Tone-downed the alt. background cyan
         // for the DATA window. C2, C3 -> C1,C2
    C2,  // BG_DATA_2
    Y8,  // FG_DATA_BYTE
    W8,  // FG_DATA_TEXT

    G4,  // BG_SYMBOLS_1
    G3,  // BG_SYMBOLS_2
    W8,  // FG_SYMBOLS_ADDRESS
    M8,  // FG_SYMBOLS_NAME

    K0,  // BG_SOURCE_TITLE
    W8,  // FG_SOURCE_TITLE
    W2,  // BG_SOURCE_1 // C2 W2 for "Paper Look"
    W3,  // BG_SOURCE_2
    W8,  // FG_SOURCE

    C3,  // BG_VIDEOSCANNER_TITLE
    W8,  // FG_VIDEOSCANNER_TITLE
    Y8,  // FG_VIDEOSCANNER_INVISIBLE
    G8,  // FG_VIDEOSCANNER_VISIBLE
};

auto DebuggerGetColor(int iColor) -> ColorRef_t {
  ColorRef_t nColor = 1;  // 0xFFFF00; // Hot Pink! -- so we notice errors. Not
                          // that there is anything wrong with pink...

  if ((g_color_scheme < NUM_COLOR_SCHEMES) && (iColor < NUM_DEBUG_COLORS)) {
    nColor = g_color_palette[g_color_index[iColor]];
  }

  return nColor;
}

auto DebuggerSetColor(const int iScheme, const int iColor,
                      const ColorRef_t nColor) -> bool {
  (void)iScheme;
  (void)iColor;
  (void)nColor;
  // color schemes ignored for Linux
  return true;
}

//===========================================================================
auto ConfigColorsReset() -> void {}

constexpr uint8_t BYTE_MASK = 0xFF;

auto ColorPrint(int iColor, ColorRef_t nColor) -> void {
  int R = static_cast<int>(nColor & BYTE_MASK);
  int G = static_cast<int>((nColor >> GREEN_SHIFT) & BYTE_MASK);
  int B = static_cast<int>((nColor >> BLUE_SHIFT) & BYTE_MASK);

  char sText[CONSOLE_WIDTH];
  ConsoleBufferPushFormat(sText, " Color %01X: %02X %02X %02X", iColor, R, G,
                          B);  // TODO: print name of colors!
}

auto CmdColorGet(const int iScheme, const int iColor) -> void {
  (void)iScheme;
  if (iColor < NUM_DEBUG_COLORS) {
    auto eColor = static_cast<DebugColors_e>(iColor);
    ColorRef_t nColor = DebuggerGetColor(eColor);
    ColorPrint(iColor, nColor);
  } else {
    fprintf(stderr, "Color: %d\nOut of range!", iColor);
  }
}
