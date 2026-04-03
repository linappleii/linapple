/*
linapple : An Apple //e emulator for Linux

Copyright (C) 2009-2010, Tom Charlesworth, Michael Pohoreski
Copyright (C) 2020, Thorsten Brehm

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Debugger custom color support
 *
 * Author: Copyright (C) 2009 - 2010 Michael Pohoreski
 */

#include "stdafx.h"

#include "Debug.h"
#include "Debugger_Color.h"

// Color ______________________________________________________________________

	int g_iColorScheme = SCHEME_COLOR;

	int g_aColorPalette[ NUM_PALETTE ] =
	{
		BLACK,
		// NOTE: See _SetupColorRamp() if you want to programmatically set/change
		RED, RED, RED, DARK_RED, DARK_RED, DARK_RED, DARK_RED, DARK_RED,  // 001 // Red
		GREEN, GREEN, MONOCHROME_GREEN, MONOCHROME_GREEN, DARK_GREEN, DARKER_GREEN, DARKER_GREEN, DARKEST_GREEN,  // 010 // Green
		YELLOW, YELLOW, YELLOW, DARK_YELLOW, DARK_YELLOW, DARKER_YELLOW, DARKER_YELLOW, DARKEST_YELLOW,  // 011 // Yellow
		BLUE, BLUE, BLUE, BLUE, DARK_BLUE, DARK_BLUE, DARKER_BLUE, DARKER_BLUE,  // 100 // Blue
		MAGENTA, MAGENTA, MAGENTA, MAGENTA, HGR_MAGENTA, HGR_MAGENTA, HGR_MAGENTA, HGR_MAGENTA,  // 101 // Magenta
		CYAN, CYAN, CYAN, CYAN, DARK_CYAN, DARK_CYAN, DARKER_CYAN, DARKEST_CYAN,  // 110 // Cyan
		WHITE, LIGHTEST_GRAY, LIGHT_GRAY, MEDIUM_GRAY, HGR_GREY1, HGR_GREY1, HGR_GREY1, HGR_GREY1,  // 111 // White/Gray

		// Custom Colors
		LIGHT_SKY_BLUE,  // Light  Sky Blue // Used for console FG
		DARKER_SKY_BLUE, // Darker Sky Blue
		DEEP_SKY_BLUE,   // Deep   Sky Blue
		ORANGE,          // Orange (Full)
		HALF_ORANGE,     // Orange (Half)
		0,
		0,
		0,

		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0
	};

	// Index into "Palette" of colors
	int g_aColorIndex[ NUM_DEBUG_COLORS ] =
	{
		K0, W8,              // BG_CONSOLE_OUTPUT   FG_CONSOLE_OUTPUT (W8)
		B2, COLOR_CUSTOM_01, // BG_CONSOLE_INPUT    FG_CONSOLE_INPUT (W8)

		B2,                  // BG_DISASM_1
		B3,                  // BG_DISASM_2

		R8, W8,              // BG_DISASM_BP_S_C    FG_DISASM_BP_S_C
		R6, W5,              // BG_DISASM_BP_0_C    FG_DISASM_BP_0_C

		R7,                  // FG_DISASM_BP_S_X    // Y8 lookes better on Info Cyan // R6
		W5,                  // FG_DISASM_BP_0_X

		W8, K0,              // BG_DISASM_C         FG_DISASM_C
		Y8, K0,              // BG_DISASM_PC_C      FG_DISASM_PC_C
		Y4, W8,              // BG_DISASM_PC_X      FG_DISASM_PC_X

		C4, W8,              // BG_DISASM_BOOKMARK  FG_DISASM_BOOKMARK

		W8,                  //                     FG_DISASM_ADDRESS
		G192,                //                     FG_DISASM_OPERATOR
		Y8,                  //                     FG_DISASM_OPCODE
		W8,                  //                     FG_DISASM_MNEMONIC
		M8,                  //                     FG_DISASM_DIRECTIVE
		COLOR_CUSTOM_04,     //                     FG_DISASM_TARGET (or W8)
		G8,                  //                     FG_DISASM_SYMBOL
		C8,                  //                     FG_DISASM_CHAR
		G8,                  //                     FG_DISASM_BRANCH

		C3,                  // BG_INFO (C4, C2 too dark)
		C3,                  // BG_INFO_WATCH
		C3,                  // BG_INFO_ZEROPAGE
		W8,                  //                     FG_INFO_TITLE (or W8)
		Y7,                  //                     FG_INFO_BULLET (W8)
		G192,                //                     FG_INFO_OPERATOR
		COLOR_CUSTOM_04,     //                     FG_INFO_ADDRESS (was Y8)
		Y8,                  //                     FG_INFO_OPCODE
		COLOR_CUSTOM_01,     //                     FG_INFO_REG (was orange)

		W8, C3,              // BG_INFO_INVERSE     FG_INFO_INVERSE
		C5,                  // BG_INFO_CHAR
		W8,                  //                     FG_INFO_CHAR_HI
		Y8,                  //                     FG_INFO_CHAR_LO

		COLOR_CUSTOM_04,     // BG_INFO_IO_BYTE
		COLOR_CUSTOM_04,     //                     FG_INFO_IO_BYTE

		C1,   // BG_DATA_1 // 2.6.2.24 Changed: Tone-downed the alt. background cyan for the DATA window. C2, C3 -> C1,C2
		C2,   // BG_DATA_2
		Y8,   // FG_DATA_BYTE
		W8,   // FG_DATA_TEXT

		G4,   // BG_SYMBOLS_1
		G3,   // BG_SYMBOLS_2
		W8,   // FG_SYMBOLS_ADDRESS
		M8,   // FG_SYMBOLS_NAME

		K0,   // BG_SOURCE_TITLE
		W8,   // FG_SOURCE_TITLE
		W2,   // BG_SOURCE_1 // C2 W2 for "Paper Look"
		W3,   // BG_SOURCE_2
		W8,   // FG_SOURCE

		C3,   // BG_VIDEOSCANNER_TITLE
		W8,   // FG_VIDEOSCANNER_TITLE
		Y8,   // FG_VIDEOSCANNER_INVISIBLE
		G8,   // FG_VIDEOSCANNER_VISIBLE
	};

unsigned int DebuggerGetColor( int iColor )
{
	unsigned int nColor = 1;// 0xFFFF00; // Hot Pink! -- so we notice errors. Not that there is anything wrong with pink...

	if ((g_iColorScheme < NUM_COLOR_SCHEMES) && (iColor < NUM_DEBUG_COLORS))
	{
		nColor = g_aColorPalette[g_aColorIndex[iColor]];
	}

	return nColor;
}


bool DebuggerSetColor( const int iScheme, const int iColor, const unsigned int nColor )
{
	// color schemes ignored for Linux
	return true;
}


#if _DEBUG
#define DEBUG_COLOR_RAMP 0
//===========================================================================
static void _SetupColorRamp(const int iPrimary, int & iColor_)
{
	char sRamp[CONSOLE_WIDTH * 2] = "";
#if DEBUG_COLOR_RAMP
	char sText[CONSOLE_WIDTH];
#endif

	bool bR = (iPrimary & 1) ? true : false;
	bool bG = (iPrimary & 2) ? true : false;
	bool bB = (iPrimary & 4) ? true : false;
	int dStep = 32;
	int nLevels = 256 / dStep;
	for (int iLevel = nLevels; iLevel > 0; iLevel--)
	{
		int nC = ((iLevel * dStep) - 1);
		int nR = bR ? nC : 0;
		int nG = bG ? nC : 0;
		int nB = bB ? nC : 0;
		unsigned int nColor = RGB(nR, nG, nB);
		g_aColorPalette[iColor_] = nColor;
#if DEBUG_COLOR_RAMP
		wsprintf(sText, "RGB(%3d,%3d,%3d, ", nR, nG, nB);
		strcat(sRamp, sText);
#endif
		iColor_++;
	}
#if DEBUG_COLOR_RAMP
	wsprintf(sText, " // %d%d%d\n", bB, bG, bR);
	strcat(sRamp, sText);
	OutputDebugString(sRamp);
	sRamp[0] = 0;
#endif
}
#endif // _DEBUG


//===========================================================================
void ConfigColorsReset(void)
{
	//	int iColor = 1; // black only has one level, skip it, since black levels same as white levels
	//	for (int iPrimary = 1; iPrimary < 8; iPrimary++ )
	//	{
	//		_SetupColorRamp( iPrimary, iColor );
	//	}

#ifdef TODO // No color schemes for Linux yet
	// Setup default colors
	int iColor;
	for (iColor = 0; iColor < NUM_DEBUG_COLORS; iColor++)
	{
		unsigned int nColor = g_aColorPalette[g_aColorIndex[iColor]];

		int R = (nColor >> 0) & 0xFF;
		int G = (nColor >> 8) & 0xFF;
		int B = (nColor >> 16) & 0xFF;

		// There are many, many ways of shifting the color domain to the monochrome domain
		// NTSC uses 3x3 matrix, could map RGB -> wavelength, etc.
		int M = (R + G + B) / 3; // Monochrome component

		int nThreshold = 64;

		int BW;
		if (M < nThreshold)
			BW = 0;
		else
			BW = 255;

		unsigned int nMono = RGB(M, M, M);
		unsigned int nBW = RGB(BW, BW, BW);

		DebuggerSetColor(SCHEME_COLOR, iColor, nColor);
		DebuggerSetColor(SCHEME_MONO, iColor, nMono);
		DebuggerSetColor(SCHEME_BW, iColor, nBW);
	}
#endif
}
