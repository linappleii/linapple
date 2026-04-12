#include "core/Common.h"
#include <cassert>
#include <algorithm>
#include <unistd.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "Debugger_Display.h"
#include "Debug.h"
#include "Debugger_Cmd_CPU.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_Cmd_Output.h"
#include "Debugger_Cmd_Config.h"
#include "Debugger_Cmd_Benchmark.h"
#include "Debugger_Cmd_ZeroPage.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Memory.h"
#include "Debugger_Symbols.h"
#include "Debugger_Assembler.h"
#include "Debugger_Parser.h"
#include "core/Util_Text.h"
#include "apple2/Video.h"
#include "frontends/sdl3/SDL_Video.h"
#include "Debugger_DisassemblerData.h"
#include "Debugger_Range.h"
#include "frontends/sdl3/AppleWin.h"
#include "apple2/CPU.h"
#include "frontends/sdl3/Frame.h"
#include "apple2/Memory.h"
#include "apple2/Mockingboard.h"
#include "apple2/stretch.h"
#include "core/asset.h"
#include <pthread.h>

#include "../../build/obj/charset40.xpm" // US/default

#define DEBUG_FORCE_DISPLAY 0

#define SOFTSWITCH_OLD 0
#define SOFTSWITCH_LANGCARD 1

#if _DEBUG
	#define DEBUG_FONT_NO_BACKGROUND_CHAR      0
	#define DEBUG_FONT_NO_BACKGROUND_TEXT      0
	#define DEBUG_FONT_NO_BACKGROUND_FILL_CON  0
	#define DEBUG_FONT_NO_BACKGROUND_FILL_INFO 0
	#define DEBUG_FONT_NO_BACKGROUND_FILL_MAIN 0
#endif

	#define DISPLAY_MEMORY_TITLE     1


// Public _________________________________________________________________________________________

	FontConfig_t g_aFontConfig[ NUM_FONTS  ];

	char g_aDebuggerVirtualTextScreen[ DEBUG_VIRTUAL_TEXT_HEIGHT ][ DEBUG_VIRTUAL_TEXT_WIDTH ];

	WindowSplit_t *g_pDisplayWindow = 0;

	VideoSurface *g_hDebugScreen;
	VideoSurface *g_hDebugCharset;
	int g_hConsoleBrushFG = 0;
	int g_hConsoleBrushBG = 0;

// Disassembly
	/*
		// Thought about moving MouseText to another location, say high bit, 'A' + 0x80
		// But would like to keep compatibility with existing CHARSET40
		// Since we should be able to display all apple chars 0x00 .. 0xFF with minimal processing
		// Use CONSOLE_COLOR_ESCAPE_CHAR to shift to mouse text
		* Apple Font
		    K      Mouse Text Up Arror
		    H      Mouse Text Left Arrow
		    J      Mouse Text Down Arrow
		* Wingdings
			\xE1   Up Arrow
			\xE2   Down Arrow
		* Webdings // M$ Font
			\x35   Up Arrow
			\x33   Left Arrow  (\x71 recycl is too small to make out details)
			\x36   Down Arrow
		* Symols
			\xAD   Up Arrow
			\xAF   Down Arrow
		* ???
			\x18 Up
			\x19 Down
	*/
#if USE_APPLE_FONT
	char * g_sConfigBranchIndicatorUp   [ NUM_DISASM_BRANCH_TYPES+1 ] = { " ", "^", "\x8B" }; // "`K" 0x4B
	char * g_sConfigBranchIndicatorEqual[ NUM_DISASM_BRANCH_TYPES+1 ] = { " ", "=", "\x88" }; // "`H" 0x48
	char * g_sConfigBranchIndicatorDown [ NUM_DISASM_BRANCH_TYPES+1 ] = { " ", "v", "\x8A" }; // "`J" 0x4A
#else
	char * g_sConfigBranchIndicatorUp   [ NUM_DISASM_BRANCH_TYPES+1 ] = { " ", "^", "\x35" };
	char * g_sConfigBranchIndicatorEqual[ NUM_DISASM_BRANCH_TYPES+1 ] = { " ", "=", "\x33" };
	char * g_sConfigBranchIndicatorDown [ NUM_DISASM_BRANCH_TYPES+1 ] = { " ", "v", "\x36" };
#endif

// Drawing
	// Width
		const int DISPLAY_WIDTH  = 560;
		// New Font = 50.5 char * 7 px/char = 353.5
		const int DISPLAY_DISASM_RIGHT   = 353 ;

#if USE_APPLE_FONT
		const int INFO_COL_1 = (51 * CONSOLE_FONT_WIDTH);
		const int DISPLAY_REGS_COLUMN       = INFO_COL_1;
		const int DISPLAY_FLAG_COLUMN       = INFO_COL_1;
		const int DISPLAY_STACK_COLUMN      = INFO_COL_1;
		const int DISPLAY_TARGETS_COLUMN    = INFO_COL_1;
		const int DISPLAY_ZEROPAGE_COLUMN   = INFO_COL_1;
		const int DISPLAY_SOFTSWITCH_COLUMN = INFO_COL_1 - (CONSOLE_FONT_WIDTH/2) + 1; // 1/2 char width padding around soft switches

		const int INFO_COL_2 = (62 * 7);
		const int DISPLAY_BP_COLUMN      = INFO_COL_2;
		const int DISPLAY_WATCHES_COLUMN = INFO_COL_2;

		const int INFO_COL_3 = (63 * 7);
		const int DISPLAY_MINIMEM_COLUMN = INFO_COL_3;
		const int DISPLAY_VIDEO_SCANNER_COLUMN = INFO_COL_3;
#else
		const int DISPLAY_CPU_INFO_LEFT_COLUMN = SCREENSPLIT1

		const int DISPLAY_REGS_COLUMN       = DISPLAY_CPU_INFO_LEFT_COLUMN;
		const int DISPLAY_FLAG_COLUMN       = DISPLAY_CPU_INFO_LEFT_COLUMN;
		const int DISPLAY_STACK_COLUMN      = DISPLAY_CPU_INFO_LEFT_COLUMN;
		const int DISPLAY_TARGETS_COLUMN    = DISPLAY_CPU_INFO_LEFT_COLUMN;
		const int DISPLAY_ZEROPAGE_COLUMN   = DISPLAY_CPU_INFO_LEFT_COLUMN;
		const int DISPLAY_SOFTSWITCH_COLUMN = DISPLAY_CPU_INFO_LEFT_COLUMN - (CONSOLE_FONT_WIDTH/2);

		const int SCREENSPLIT2 = SCREENSPLIT1 + (12 * 7); // moved left 3 chars to show B. prefix in breakpoint #, W. prefix in watch #
		const int DISPLAY_BP_COLUMN      = SCREENSPLIT2;
		const int DISPLAY_WATCHES_COLUMN = SCREENSPLIT2;
		const int DISPLAY_MINIMEM_COLUMN = SCREENSPLIT2;
		const int DISPLAY_VIDEO_SCANNER_COLUMN = SCREENSPLIT2;
#endif

		int MAX_DISPLAY_REGS_LINES        = 7;
		int MAX_DISPLAY_STACK_LINES       = 8;
		int MAX_DISPLAY_TARGET_PTR_LINES  = 2;
		int MAX_DISPLAY_ZEROPAGE_LINES    = 8;

		int MAX_DISPLAY_MEMORY_LINES_1    = 4;
		int MAX_DISPLAY_MEMORY_LINES_2    = 4;
		int g_nDisplayMemoryLines;

		VideoScannerDisplayInfo g_videoScannerDisplayInfo;

char ColorizeSpecialChar( char * sText, unsigned char nData, const MemoryView_e iView,
			const int iTxtBackground, const int iTxtForeground,
			const int iHighBackground, const int iHighForeground,
			const int iLowBackground, const int iLowForeground );

	char  FormatCharTxtAsci ( const unsigned char b, bool * pWasAsci_ );

	void DrawSubWindow_Code ( int iWindow );

	void DrawWindowBottom ( Update_t bUpdate, int iWindow );

	char* FormatCharCopy( char *pDst, const char *pSrc, const int nLen );
	char  FormatCharTxtAsci( const unsigned char b, bool * pWasAsci_ = NULL );
	char  FormatCharTxtCtrl( const unsigned char b, bool * pWasCtrl_ = NULL );
	char  FormatCharTxtHigh( const unsigned char b, bool *pWasHi_ = NULL );
	char  FormatChar4Font  ( const unsigned char b, bool *pWasHi_, bool *pWasLo_ );

	void DrawRegister(int line, const char* name, int bytes, unsigned short value, int iSource = 0);

#define  SOFTSTRECH(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, DST_W, DST_H) \
{ \
  srcrect.x = SRC_X; \
  srcrect.y = SRC_Y; \
  srcrect.w = SRC_W; \
  srcrect.h = SRC_H; \
  dstrect.x = DST_X; \
  dstrect.y = DST_Y; \
  dstrect.w = DST_W; \
  dstrect.h = DST_H; \
  SDL_SoftStretch(SRC, &srcrect, DST, &dstrect);\
}

#define  SOFTSTRECH_MONO(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, DST_W, DST_H) \
{ \
  srcrect.x = SRC_X; \
  srcrect.y = SRC_Y; \
  srcrect.w = SRC_W; \
  srcrect.h = SRC_H; \
  dstrect.x = DST_X; \
  dstrect.y = DST_Y; \
  dstrect.w = DST_W; \
  dstrect.h = DST_H; \
  VideoSoftStretchMono8(SRC, &srcrect, DST, &dstrect, hBrush, hBgBrush);\
}

//===========================================================================

void AllocateDebuggerMemDC(void)
{
  if (!g_hDebugScreen)
  {
    g_hDebugScreen = VideoCreateSurface(560, 384, 1);
    g_hDebugCharset = VideoLoadXPM(charset40_xpm);
  }
}

void ReleaseDebuggerMemDC(void)
{
}

void GetDebugViewPortScale(float *x, float *y)
{
  if (!g_hDebugScreen) {
    *x = 1.0f; *y = 1.0f;
    return;
  }
	float f = ((float) g_hDebugScreen->w) / SCREEN_WIDTH;
	*x = (f>0.01) ? f : 0.01;
	f = ((float) g_hDebugScreen->h) / SCREEN_HEIGHT;
	*y = (f>0.01) ? f : 0.01;
}

void StretchBltMemToFrameDC(void)
{
	VideoRect drect, srect;

	g_video_draw_mutex.lock();

  if (!g_hDebugScreen) {
    g_video_draw_mutex.unlock();
    return;
  }

	drect.x = drect.y = srect.x = srect.y = 0;
	drect.w = 560; // Assuming screen resolution
	drect.h = 384;
	srect.w = g_hDebugScreen->w;
	srect.h = g_hDebugScreen->h;

	VideoSurface vs_screen = SDLSurfaceToVideoSurface(screen);
	VideoSoftStretch(g_hDebugScreen, &srect, g_origscreen, &drect);
	VideoSoftStretch(g_origscreen, NULL, &vs_screen, NULL);

	g_video_draw_mutex.unlock();
}

// Font: Apple Text
void DebuggerSetColorFG( unsigned int nRGB )
{
	g_hConsoleBrushFG = nRGB;
}

void DebuggerSetColorBG( unsigned int nRGB, bool bTransparent )
{
	(void) bTransparent;
	g_hConsoleBrushBG = nRGB;
}

#define CONSOLE_FONT_GRID_X  8
#define CONSOLE_FONT_GRID_Y  8
#define CONSOLE_FONT_WIDTH   7
#define CONSOLE_FONT_HEIGHT  8

//SDL_Color debugColors[256];
extern int g_hConsoleBrushFG;
extern int g_hConsoleBrushBG;

void FillRect(const RECT *r, int Brush)
{
  if (!g_hDebugScreen) {
    return;
  }
	rectangle(g_hDebugScreen, r->left, r->top, r->right - r->left, r->bottom - r->top, Brush);
}


// @param glyph Specifies a native glyph from the 16x16 chars Apple Font Texture.
//===========================================================================
void PrintGlyph( const int x, const int y, const char glyph )
{
	char g = glyph;

	int ySrc = 64;

	if (glyph<32)
	{
		// mouse text
		g -= 32;
		ySrc = 48;
	}
	else
	if ((glyph >= '@')&&(glyph <= '_'))
	{
		g -= '@';
	}
	else
	if ((glyph >= ' ')&&(glyph <= '?'))
	{
		g += 32-' ';
	}
	else
	if ((glyph >= '`')&&((unsigned char)glyph <= 127))
	{
		g += 6*16 - '`';
	}

	int xSrc = (g & 0x0F) * CONSOLE_FONT_GRID_X;
	ySrc += ((g >>   4) * CONSOLE_FONT_GRID_Y);

	{
#if _DEBUG
		if ((x < 0) || (y < 0))
			fprintf(stderr, "X or Y out of bounds: PrintGlyph(x,y)", x, y);
#endif
		int col = x / CONSOLE_FONT_WIDTH ;
		int row = y / CONSOLE_FONT_HEIGHT;

		if (x > DISPLAY_DISASM_RIGHT)
			col++;

		if ((col < DEBUG_VIRTUAL_TEXT_WIDTH)
		&&  (row < DEBUG_VIRTUAL_TEXT_HEIGHT))
			g_aDebuggerVirtualTextScreen[ row ][ col ] = glyph;
	}

	VideoRect srcrect, dstrect;

	uint8_t hBrush = g_hConsoleBrushFG;
	uint8_t hBgBrush = g_hConsoleBrushBG;
	SOFTSTRECH_MONO(g_hDebugCharset, xSrc, ySrc, CONSOLE_FONT_WIDTH, CONSOLE_FONT_HEIGHT,
		g_hDebugScreen, x, y, CONSOLE_FONT_WIDTH, CONSOLE_FONT_HEIGHT);
}

//===========================================================================
void DebuggerPrint ( int x, int y, const char *pText )
{
	const int nLeft = x;

	char c;
	const char *p = pText;

	while ((c = *p))
	{
		if (c == '\n')
		{
			x = nLeft;
			y += CONSOLE_FONT_HEIGHT;
			p++;
			continue;
		}
		c &= 0x7F;
		PrintGlyph( x, y, c );
		x += CONSOLE_FONT_WIDTH;
		p++;
	}
}

//===========================================================================
void DebuggerPrintColor( int x, int y, const conchar_t * pText )
{
	int nLeft = x;

	conchar_t g;
	const conchar_t *pSrc = pText;

	if( !pText)
		return;

	while ((g = (*pSrc)))
	{
		if (g == '\n')
		{
			x = nLeft;
			y += CONSOLE_FONT_HEIGHT;
			pSrc++;
			continue;
		}

		if (ConsoleColor_IsColorOrMouse( g ))
		{
			if (ConsoleColor_IsColor( g ))
			{
				DebuggerSetColorFG( ConsoleColor_GetColor( g ) );
			}

			g = ConsoleChar_GetChar( g );
		}

		PrintGlyph( x, y, (char) (g & _CONSOLE_COLOR_MASK) );
		x += CONSOLE_FONT_WIDTH;
		pSrc++;
	}
}


// Utility ________________________________________________________________________________________


//===========================================================================
bool CanDrawDebugger()
{
	return (g_state.mode == MODE_DEBUG) || (g_state.mode == MODE_STEPPING);
}


//===========================================================================
int PrintText ( const char * pText, RECT & rRect )
{
#if _DEBUG
	if (! pText)
		fprintf(stderr, "%s: %s\n", "DrawText()", "pText = NULL!");
#endif

	int nLen = strlen( pText );

#if !DEBUG_FONT_NO_BACKGROUND_TEXT
	FillRect(&rRect, g_hConsoleBrushBG );
#endif

	DebuggerPrint( rRect.left, rRect.top, pText );
	return nLen;
}

//===========================================================================
void PrintTextColor ( const conchar_t *pText, RECT & rRect )
{
#if !DEBUG_FONT_NO_BACKGROUND_TEXT
	FillRect(&rRect, g_hConsoleBrushBG );
#endif

	DebuggerPrintColor( rRect.left, rRect.top, pText );
}

// Updates the horizontal cursor
//===========================================================================
int PrintTextCursorX ( const char * pText, RECT & rRect )
{
	int nChars = 0;
	if (pText)
	{
		nChars = PrintText( pText, rRect );
		int nFontWidth = g_aFontConfig[ FONT_DISASM_DEFAULT ]._nFontWidthAvg;
		rRect.left += (nFontWidth * nChars);
	}
	return nChars;
}

//===========================================================================
int PrintTextCursorY ( const char * pText, RECT & rRect )
{
	int nChars = PrintText( pText, rRect );
	rRect.top    += g_nFontHeight;
	rRect.bottom += g_nFontHeight;
	return nChars;
}


//===========================================================================
char* FormatCharCopy( char *pDst, const char *pSrc, const int nLen )
{
	for( int i = 0; i < nLen; i++ )
		*pDst++ = FormatCharTxtCtrl( *pSrc++ );
	return pDst;
}

//===========================================================================
char  FormatCharTxtAsci ( const unsigned char b, bool * pWasAsci_ )
{
	if (pWasAsci_)
		*pWasAsci_ = false;

	char c = (b & 0x7F);
	if (b <= 0x7F)
	{
		if (pWasAsci_)
		{
			*pWasAsci_ = true;
		}
	}
	return c;
}

// Note: FormatCharTxtCtrl() and RemapChar()
//===========================================================================
char  FormatCharTxtCtrl ( const unsigned char b, bool * pWasCtrl_ )
{
	if (pWasCtrl_)
		*pWasCtrl_ = false;

	char c = (b & 0x7F); // .32 Changed: Lo now maps High Ascii to printable chars. i.e. ML1 D0D0
	if (b < 0x20) // SPACE
	{
		if (pWasCtrl_)
		{
			*pWasCtrl_ = true;
		}
		c = b + '@'; // map ctrl chars to visible
	}
	return c;
}

//===========================================================================
char  FormatCharTxtHigh ( const unsigned char b, bool *pWasHi_ )
{
	if (pWasHi_)
		*pWasHi_ = false;

	char c = b;
	if (b > 0x7F)
	{
		if (pWasHi_)
		{
			*pWasHi_ = true;
		}
		c = (b & 0x7F);
	}
	return c;
}


//===========================================================================
char FormatChar4Font ( const unsigned char b, bool *pWasHi_, bool *pWasLo_ )
{
	// Most Windows Fonts don't have (printable) glyphs for control chars
	unsigned char b1 = FormatCharTxtHigh( b , pWasHi_ );
	unsigned char b2 = FormatCharTxtCtrl( b1, pWasLo_ );
	return b2;
}




//===========================================================================
void SetupColorsHiLoBits ( bool bHighBit, bool bCtrlBit,
	const int iBackground, const int iForeground,
	const int iColorHiBG , const int iColorHiFG,
	const int iColorLoBG , const int iColorLoFG )
{
	// 4 cases:
	// Hi Lo Background Foreground -> just map Lo -> FG, Hi -> BG
	// 0  0  normal     normal     BG_INFO        FG_DISASM_CHAR   (dark cyan bright cyan)
	// 0  1  normal     LoFG       BG_INFO        FG_DISASM_OPCODE (dark cyan yellow)
	// 1  0  HiBG       normal     BG_INFO_CHAR   FG_DISASM_CHAR   (mid cyan  bright cyan)
	// 1  1  HiBG       LoFG       BG_INFO_CHAR   FG_DISASM_OPCODE (mid cyan  yellow)

	DebuggerSetColorBG( DebuggerGetColor( iBackground ));
	DebuggerSetColorFG( DebuggerGetColor( iForeground ));

	if (bHighBit)
	{
		DebuggerSetColorBG( DebuggerGetColor( iColorHiBG ));
		DebuggerSetColorFG( DebuggerGetColor( iColorHiFG )); // was iForeground
	}

	if (bCtrlBit)
	{
		DebuggerSetColorBG( DebuggerGetColor( iColorLoBG ));
		DebuggerSetColorFG( DebuggerGetColor( iColorLoFG ));
	}
}


// To flush out color bugs... swap: iAsciBackground & iHighBackground
//===========================================================================
char ColorizeSpecialChar( char * sText, unsigned char nData, const MemoryView_e iView,
		const int iAsciBackground, const int iTextForeground,
		const int iHighBackground, const int iHighForeground,
		const int iCtrlBackground, const int iCtrlForeground )
{
	bool bHighBit = false;
	bool bCtrlBit = false;

	int iTextBG = iAsciBackground;
	int iHighBG = iHighBackground;
	int iCtrlBG = iCtrlBackground;
	int iTextFG = iTextForeground;
	int iHighFG = iHighForeground;
	int iCtrlFG = iCtrlForeground;

	unsigned char nByte = FormatCharTxtHigh( nData, & bHighBit );
	char nChar = FormatCharTxtCtrl( nByte, & bCtrlBit );

	switch (iView)
	{
		case MEM_VIEW_ASCII:
			iHighBG = iTextBG;
			iCtrlBG = iTextBG;
			break;
		case MEM_VIEW_APPLE:
			iHighBG = iTextBG;
			if (!bHighBit)
			{
				iTextBG = iCtrlBG;
			}

			if (bCtrlBit)
			{
				iTextFG = iCtrlFG;
				if (bHighBit)
				{
					iHighFG = iTextFG;
				}
			}
			bCtrlBit = false;
			break;
		default: break;
	}

	if (sText)
		sprintf( sText, "%c", nChar );

#if OLD_CONSOLE_COLOR
	if (sText)
	{
		if (ConsoleColor_IsEscapeMeta( nChar ))
			sprintf( sText, "%c%c", nChar, nChar );
		else
			sprintf( sText, "%c", nChar );
	}
#endif

//	if (hDC)
	{
		SetupColorsHiLoBits( bHighBit, bCtrlBit
			, iTextBG, iTextFG // FG_DISASM_CHAR
			, iHighBG, iHighFG // BG_INFO_CHAR
			, iCtrlBG, iCtrlFG // FG_DISASM_OPCODE
		);
	}
	return nChar;
}

void ColorizeFlags( bool bSet, int bg_default = BG_INFO, int fg_default = FG_INFO_TITLE )
{
	if (bSet)
	{
		DebuggerSetColorBG( DebuggerGetColor( BG_INFO_INVERSE ));
		DebuggerSetColorFG( DebuggerGetColor( FG_INFO_INVERSE ));
	}
	else
	{
		DebuggerSetColorBG( DebuggerGetColor( bg_default ));
		DebuggerSetColorFG( DebuggerGetColor( fg_default ));
	}
}


// Main Windows ___________________________________________________________________________________


//===========================================================================
void DrawBreakpoints ( int line )
{
	if ((g_iWindowThis != WINDOW_CODE) && !((g_iWindowThis == WINDOW_DATA)))
		return;

	RECT rect;
	rect.left   = DISPLAY_BP_COLUMN;
	rect.top    = (line * g_nFontHeight);
	rect.right  = DISPLAY_WIDTH;
	rect.bottom = rect.top + g_nFontHeight;

	char sText[16] = "Breakpoints";

#if DISPLAY_BREAKPOINT_TITLE
	DebuggerSetColorBG( DebuggerGetColor( BG_INFO ));
	DebuggerSetColorFG( DebuggerGetColor( FG_INFO_TITLE ));
	PrintText( sText, rect );
	rect.top    += g_nFontHeight;
	rect.bottom += g_nFontHeight;
#endif

	int nBreakpointsDisplayed = 0;

	int iBreakpoint;
	for (iBreakpoint = 0; iBreakpoint < MAX_BREAKPOINTS; iBreakpoint++ )
	{
		Breakpoint_t *pBP = &g_aBreakpoints[iBreakpoint];
		unsigned int nLength = pBP->nLength;

		if (nLength)
		{
			bool bSet      = pBP->bSet;
			bool bEnabled  = pBP->bEnabled;
			unsigned short nAddress1 = pBP->nAddress;
			unsigned short nAddress2 = nAddress1 + nLength - 1;

			if (! bSet)
				continue;

			nBreakpointsDisplayed++;

			RECT rect2;
			rect2 = rect;

			DebuggerSetColorBG( DebuggerGetColor( BG_INFO ));
			DebuggerSetColorFG( DebuggerGetColor( FG_INFO_TITLE ) );
			sprintf( sText, "B" );
			PrintTextCursorX( sText, rect2 );

			DebuggerSetColorBG( DebuggerGetColor( BG_INFO ));
			DebuggerSetColorFG( DebuggerGetColor( FG_INFO_BULLET ) );
			sprintf( sText, "%X ", iBreakpoint );
			PrintTextCursorX( sText, rect2 );

			DebuggerSetColorFG( DebuggerGetColor( FG_INFO_REG ) );
			int nRegLen = PrintTextCursorX( g_aBreakpointSource[ pBP->eSource ], rect2 );

			// Pad to 2 chars
			if (nRegLen < 2)
				rect2.left += g_aFontConfig[ FONT_INFO ]._nFontWidthAvg;

			DebuggerSetColorBG( DebuggerGetColor( BG_INFO ));
			DebuggerSetColorFG( DebuggerGetColor( FG_INFO_BULLET ) );
			PrintTextCursorX( g_aBreakpointSymbols [ pBP->eOperator ], rect2 );

			DebugColors_e iForeground;
			DebugColors_e iBackground = BG_INFO;

			if (bSet)
			{
				if (bEnabled)
				{
					iBackground = BG_DISASM_BP_S_C;
					iForeground = FG_DISASM_BP_S_C;
				}
				else
				{
					iForeground = FG_DISASM_BP_0_X;
				}
			}
			else
			{
				iForeground = FG_INFO_TITLE;
			}

			DebuggerSetColorBG( DebuggerGetColor( iBackground ) );
			DebuggerSetColorFG( DebuggerGetColor( iForeground ) );

			sprintf( sText, "%04X", nAddress1 );
			PrintTextCursorX( sText, rect2 );

			if (nLength == 1)
			{
				if (pBP->eSource == BP_SRC_MEM_READ_ONLY)
					PrintTextCursorX("R", rect2);
				else if (pBP->eSource == BP_SRC_MEM_WRITE_ONLY)
					PrintTextCursorX("W", rect2);
			}

			if (nLength > 1)
			{
				DebuggerSetColorBG( DebuggerGetColor( BG_INFO ) );
				DebuggerSetColorFG( DebuggerGetColor( FG_INFO_OPERATOR ) );

				PrintTextCursorX( ":", rect2 );

				DebuggerSetColorBG( DebuggerGetColor( iBackground ) );
				DebuggerSetColorFG( DebuggerGetColor( iForeground ) );

				sprintf( sText, "%04X", nAddress2 );
				PrintTextCursorX( sText, rect2 );

				if (pBP->eSource == BP_SRC_MEM_READ_ONLY)
					PrintTextCursorX("R", rect2);
				else if (pBP->eSource == BP_SRC_MEM_WRITE_ONLY)
					PrintTextCursorX("W", rect2);
			}

			DebuggerSetColorBG( DebuggerGetColor( BG_INFO ));
			DebuggerSetColorFG( DebuggerGetColor( FG_INFO_TITLE ));
			PrintTextCursorX( " ", rect2 );
			rect.top    += g_nFontHeight;
			rect.bottom += g_nFontHeight;
		}
	}
}


// Console ________________________________________________________________________________________


//===========================================================================
int GetConsoleLineHeightPixels()
{
	int nHeight = g_aFontConfig[ FONT_CONSOLE ]._nFontHeight; // _nLineHeight; // _nFontHeight;
/*
	if (g_iFontSpacing == FONT_SPACING_CLASSIC)
	{
		nHeight++;  // "Classic" Height/Spacing
	}
	else
	if (g_iFontSpacing == FONT_SPACING_CLEAN)
	{
		nHeight++;
	}
	else
	if (g_iFontSpacing == FONT_SPACING_COMPRESSED)
	{
		// default case handled
	}
*/
	return nHeight;
}

//===========================================================================
int GetConsoleTopPixels( int y )
{
	int nLineHeight = GetConsoleLineHeightPixels();
	int nTop = DISPLAY_HEIGHT - ((y + 1) * nLineHeight);
	return nTop;
}

//===========================================================================
void DrawConsoleCursor ()
{
	DebuggerSetColorFG( DebuggerGetColor( FG_CONSOLE_INPUT ));
	DebuggerSetColorBG( DebuggerGetColor( BG_CONSOLE_INPUT ));

	int nWidth = g_aFontConfig[ FONT_CONSOLE ]._nFontWidthAvg;

	int nLineHeight = GetConsoleLineHeightPixels();
	int y = 0;

	RECT rect;
	rect.left   = (g_nConsoleInputChars + g_nConsolePromptLen) * nWidth;
	rect.top    = GetConsoleTopPixels( y );
	rect.bottom = rect.top + nLineHeight; //g_nFontHeight;
	rect.right  = rect.left + nWidth;

	PrintText( g_sConsoleCursor, rect );
}

//===========================================================================
void GetConsoleRect ( const int y, RECT & rect )
{
	int nLineHeight = GetConsoleLineHeightPixels();

	rect.left   = 0;
	rect.top    = GetConsoleTopPixels( y );
	rect.bottom = rect.top + nLineHeight; //g_nFontHeight;

//	int nHeight = WindowGetHeight( g_iWindowThis );

	int nFontWidth = g_aFontConfig[ FONT_CONSOLE ]._nFontWidthAvg;
	int nMiniConsoleRight = g_nConsoleDisplayWidth * nFontWidth;
	int nFullConsoleRight = DISPLAY_WIDTH;
	int nRight = g_bConsoleFullWidth ? nFullConsoleRight : nMiniConsoleRight;
	rect.right = nRight;
}

//===========================================================================
void DrawConsoleLine ( const conchar_t * pText, int y )
{
	if (y < 0)
		return;

	RECT rect;
	GetConsoleRect( y, rect );

	// Console background is drawn in DrawWindowBackground_Info
	PrintTextColor( pText, rect );
}

//===========================================================================
void DrawConsoleInput ()
{
	DebuggerSetColorFG( DebuggerGetColor( FG_CONSOLE_INPUT ));
	DebuggerSetColorBG( DebuggerGetColor( BG_CONSOLE_INPUT ));

	RECT rect;
	GetConsoleRect( 0, rect );

	// Console background is drawn in DrawWindowBackground_Info
//	DrawConsoleLine( g_aConsoleInput, 0 );
	PrintText( g_aConsoleInput, rect );
}


// Disassembly ____________________________________________________________________________________

void GetTargets_IgnoreDirectJSRJMP(const unsigned char iOpcode, int& nTargetPointer)
{
	if (iOpcode == OPCODE_JSR || iOpcode == OPCODE_JMP_A)
		nTargetPointer = NO_6502_TARGET;
}

// Get the data needed to disassemble one line of opcodes. Fills in the DisasmLine info.
// Disassembly formatting flags returned
//	@parama sTargetValue_ indirect/indexed final value
//===========================================================================
int GetDisassemblyLine ( unsigned short nBaseAddress, DisasmLine_t & line_ )
{
	line_.Clear();

	int iOpcode;
	int iOpmode;
	int nOpbyte;

	iOpcode = _6502_GetOpmodeOpbyte( nBaseAddress, iOpmode, nOpbyte, &line_.pDisasmData );
	const DisasmData_t* pData = line_.pDisasmData; // Disassembly_IsDataAddress( nBaseAddress );

	line_.iOpcode = iOpcode;
	line_.iOpmode = iOpmode;
	line_.nOpbyte = nOpbyte;

#if _DEBUG
//	if (iLine != 41)
//		return nOpbytes;
#endif

	if (iOpmode == AM_M)
		line_.bTargetImmediate = true;

	if ((iOpmode >= AM_IZX) && (iOpmode <= AM_NA))
		line_.bTargetIndirect = true;

	if ((iOpmode >= AM_IZX) && (iOpmode <= AM_NZY))
		line_.bTargetIndexed = true;

	if (((iOpmode >= AM_A) && (iOpmode <= AM_ZY)) || line_.bTargetIndirect)
		line_.bTargetValue = true;

	if ((iOpmode == AM_AX) || (iOpmode == AM_ZX) || (iOpmode == AM_IZX) || (iOpmode == AM_IAX))
		line_.bTargetX = true;

	if ((iOpmode == AM_AY) || (iOpmode == AM_ZY) || (iOpmode == AM_NZY))
		line_.bTargetY = true;

	unsigned int nMinBytesLen = (MAX_OPCODES * (2 + g_bConfigDisasmOpcodeSpaces));

	int bDisasmFormatFlags = 0;

	unsigned short nTarget = 0;

	if ((iOpmode != AM_IMPLIED) &&
		(iOpmode != AM_1) &&
		(iOpmode != AM_2) &&
		(iOpmode != AM_3))
	{
		if( pData )
		{
			nTarget = pData->nTargetAddress;
		} else {
			nTarget = *(uint16_t*)(mem+nBaseAddress+1);
			if (nOpbyte == 2)
				nTarget &= 0xFF;
		}

		if (iOpmode == AM_R)
		{
			line_.bTargetRelative = true;

			nTarget = nBaseAddress+2+(int)(signed char)nTarget;

			line_.nTarget = nTarget;
			sprintf( line_.sTargetValue, "%04X", nTarget & 0xFFFF );

			bDisasmFormatFlags |= DISASM_FORMAT_BRANCH;

			if (nTarget < nBaseAddress)
			{
				sprintf( line_.sBranch, "%s", g_sConfigBranchIndicatorUp[ g_iConfigDisasmBranchType ] );
			}
			else
			if (nTarget > nBaseAddress)
			{
				sprintf( line_.sBranch, "%s", g_sConfigBranchIndicatorDown[ g_iConfigDisasmBranchType ] );
			}
			else
			{
				sprintf( line_.sBranch, "%s", g_sConfigBranchIndicatorEqual[ g_iConfigDisasmBranchType ] );
			}
		}

		if ((iOpmode == AM_A  ) ||
			(iOpmode == AM_Z  ) ||
			(iOpmode == AM_AX ) ||
			(iOpmode == AM_AY ) ||
			(iOpmode == AM_ZX ) ||
			(iOpmode == AM_ZY ) ||
			(iOpmode == AM_R  ) ||
			(iOpmode == AM_IZX) ||
			(iOpmode == AM_IAX) ||
			(iOpmode == AM_NZY) ||
			(iOpmode == AM_NZ ) ||
			(iOpmode == AM_NA ))
		{
			line_.nTarget  = nTarget;

			const char* pTarget = NULL;
			const char* pSymbol = 0;

			pSymbol = FindSymbolFromAddress( nTarget );

			if( pData && (!pData->bSymbolLookup))
				pSymbol = 0;

			if (pSymbol)
			{
				bDisasmFormatFlags |= DISASM_FORMAT_SYMBOL;
				pTarget = pSymbol;
			}

			if (! (bDisasmFormatFlags & DISASM_FORMAT_SYMBOL))
			{
				pSymbol = FindSymbolFromAddress( nTarget - 1 );
				if (pSymbol)
				{
					bDisasmFormatFlags |= DISASM_FORMAT_SYMBOL;
					bDisasmFormatFlags |= DISASM_FORMAT_OFFSET;
					pTarget = pSymbol;
					line_.nTargetOffset = +1;
				}
			}

			if (! (bDisasmFormatFlags & DISASM_FORMAT_SYMBOL) || pData)
			{
				pSymbol = FindSymbolFromAddress( nTarget + 1 );
				if (pSymbol)
				{
					bDisasmFormatFlags |= DISASM_FORMAT_SYMBOL;
					bDisasmFormatFlags |= DISASM_FORMAT_OFFSET;
					pTarget = pSymbol;
					line_.nTargetOffset = -1;
				}
			}

			if (! (bDisasmFormatFlags & DISASM_FORMAT_SYMBOL))
			{
				pTarget = FormatAddress( nTarget, (iOpmode != AM_R) ? nOpbyte : 3 );	// GH#587: For Bcc opcodes, pretend it's a 3-byte opcode to print a 16-bit target addr
			}

			if (bDisasmFormatFlags & DISASM_FORMAT_OFFSET)
			{
				int nAbsTargetOffset =  (line_.nTargetOffset > 0) ? line_.nTargetOffset : - line_.nTargetOffset;
				snprintf( line_.sTargetOffset, sizeof(line_.sTargetOffset), "%d", nAbsTargetOffset );
			}
			snprintf( line_.sTarget, sizeof(line_.sTarget), "%s", pTarget );


			int nTargetPartial;
			int nTargetPartial2;
			int nTargetPointer;
			unsigned short nTargetValue = 0;
			_6502_GetTargets( nBaseAddress, &nTargetPartial, &nTargetPartial2, &nTargetPointer, NULL );
			GetTargets_IgnoreDirectJSRJMP(iOpcode, nTargetPointer);	// For *direct* JSR/JMP, don't show 'addr16:byte char'

			if (nTargetPointer != NO_6502_TARGET)
			{
				bDisasmFormatFlags |= DISASM_FORMAT_TARGET_POINTER;

				nTargetValue = *(mem + nTargetPointer) | (*(mem + ((nTargetPointer + 1) & 0xffff)) << 8);

				if (g_iConfigDisasmTargets & DISASM_TARGET_ADDR)
					sprintf( line_.sTargetPointer, "%04X", nTargetPointer & 0xFFFF );

				if (iOpcode != OPCODE_JMP_NA && iOpcode != OPCODE_JMP_IAX)
				{
					bDisasmFormatFlags |= DISASM_FORMAT_TARGET_VALUE;
					if (g_iConfigDisasmTargets & DISASM_TARGET_VAL)
						sprintf( line_.sTargetValue, "%02X", nTargetValue & 0xFF );

					bDisasmFormatFlags |= DISASM_FORMAT_CHAR;
					line_.nImmediate = (unsigned char) nTargetValue;

					unsigned _char = FormatCharTxtCtrl( FormatCharTxtHigh( line_.nImmediate, NULL ), NULL );
					sprintf( line_.sImmediate, "%c", _char );
				}
			}
		}
		else
		if (iOpmode == AM_M)
		{
			sprintf( line_.sTarget, "%02X", (unsigned)nTarget );

			if (iOpmode == AM_M)
			{
				bDisasmFormatFlags |= DISASM_FORMAT_CHAR;
				line_.nImmediate = (unsigned char) nTarget;
				unsigned _char = FormatCharTxtCtrl( FormatCharTxtHigh( line_.nImmediate, NULL ), NULL );

				sprintf( line_.sImmediate, "%c", _char );
			}
		}
	}

	sprintf( line_.sAddress, "%04X", nBaseAddress );

	// Opcode Bytes
	FormatOpcodeBytes( nBaseAddress, line_ );

// Data Disassembler
	if( pData )
	{
		line_.iNoptype = pData->eElementType;
		line_.iNopcode = pData->iDirective;
		strcpy( line_.sMnemonic, g_aAssemblerDirectives[ line_.iNopcode ].m_pMnemonic );

		FormatNopcodeBytes( nBaseAddress, line_ );
	} else { // Regular 6502/65C02 opcode -> mnemonic
		strcpy( line_.sMnemonic, g_aOpcodes[ line_.iOpcode ].sMnemonic );
	}

	int nSpaces = strlen( line_.sOpCodes );
    while (nSpaces < (int)nMinBytesLen)
	{
		strcat( line_.sOpCodes, " " );
		nSpaces++;
	}

	return bDisasmFormatFlags;
}


const char* FormatAddress( unsigned short nAddress, int nBytes )
{
	// There is no symbol for this nAddress
	static char sSymbol[8] = "";
	switch (nBytes)
	{
		case  2:	snprintf(sSymbol, 8, "$%02X",(unsigned)nAddress);  break;
		case  3:	snprintf(sSymbol, 8, "$%04X",(unsigned)nAddress);  break;
		// TODO: FIXME: Can we get called with nBytes == 16 ??
		default:	sSymbol[0] = 0; break; // clear since is static
	}
	return sSymbol;
}

void FormatOpcodeBytes ( unsigned short nBaseAddress, DisasmLine_t & line_ )
{
	int nOpbyte = line_.nOpbyte;

	char *pDst = line_.sOpCodes;
	int nMaxOpBytes = nOpbyte;
	if ( nMaxOpBytes > MAX_OPCODES)
		nMaxOpBytes = MAX_OPCODES;

	for( int iByte = 0; iByte < nMaxOpBytes; iByte++ )
	{
		unsigned char nMem = (unsigned)*(mem+nBaseAddress + iByte);
		sprintf( pDst, "%02X", nMem );
		pDst += 2;

		if (g_bConfigDisasmOpcodeSpaces)
		{
			strcat( pDst, " " );
			pDst++;
		}
	}
}

void FormatNopcodeBytes ( unsigned short nBaseAddress, DisasmLine_t & line_ )
{
		char *pDst = line_.sTarget;
		const	char *pSrc = 0;
		unsigned int nStartAddress = line_.pDisasmData->nStartAddress;
		unsigned int nEndAddress   = line_.pDisasmData->nEndAddress  ;
		int   nDisplayLen   = nEndAddress - nBaseAddress  + 1 ;
		int   len           = nDisplayLen;

	for( int iByte = 0; iByte < line_.nOpbyte; )
	{
		unsigned char nTarget8  = *(uint8_t*)(mem + nBaseAddress + iByte);
		unsigned short nTarget16 = *(uint16_t*)(mem + nBaseAddress + iByte);

		switch( line_.iNoptype )
		{
			case NOP_BYTE_1:
			case NOP_BYTE_2:
			case NOP_BYTE_4:
			case NOP_BYTE_8:
				sprintf( pDst, "%02X", nTarget8 );
				pDst += 2;
				iByte++;
				if( line_.iNoptype == NOP_BYTE_1)
					if( iByte < line_.nOpbyte )
					{
						*pDst++ = ',';
					}
				break;
			case NOP_WORD_1:
			case NOP_WORD_2:
			case NOP_WORD_4:
				sprintf( pDst, "%04X", nTarget16 );
				pDst += 4;
				iByte+= 2;
				if( iByte < line_.nOpbyte )
				{
					*pDst++ = ',';
				}
				break;
			case NOP_ADDRESS:
				iByte += 2;
				break;
			case NOP_STRING_APPLESOFT:
				iByte = line_.nOpbyte;
				Util_SafeStrCpy( pDst, (const char*)(mem + nBaseAddress), iByte );
				pDst += iByte;
				*pDst = 0;
				break;
			case NOP_STRING_APPLE:
				iByte = line_.nOpbyte;
				pSrc = (const char*)mem + nStartAddress;

				if (len > (MAX_IMMEDIATE_LEN - 2))
				{
					if (len > MAX_IMMEDIATE_LEN)
						len = (MAX_IMMEDIATE_LEN - 3);

					FormatCharCopy( pDst, pSrc, len );

					if( nDisplayLen > len )
					{
						*pDst++ = '.';
						*pDst++ = '.';
						*pDst++ = '.';
					}
				} else {
					*pDst++ = '"';
					pDst = FormatCharCopy( pDst, pSrc, len );
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

//===========================================================================
void FormatDisassemblyLine( const DisasmLine_t & line, char * sDisassembly, const int nBufferSize )
{
  (void)nBufferSize;
	//> Address Seperator Opcodes   Label Mnemonic Target [Immediate] [Branch]
	//
	// Data Disassembler
	//                              Label Directive       [Immediate]
	const char * pMnemonic = g_aOpcodes[ line.iOpcode ].sMnemonic;

	sprintf( sDisassembly, "%s:%s %s "
		, line.sAddress
		, line.sOpCodes
		, pMnemonic
	);

/*
	if (line.bTargetIndexed || line.bTargetIndirect)
	{
		strcat( sDisassembly, "(" );
	}

	if (line.bTargetImmediate)
		strcat( sDisassembly, "#$" );

	if (line.bTargetValue)
		strcat( sDisassembly, line.sTarget );

	if (line.bTargetIndirect)
	{
		if (line.bTargetX)
	 		strcat( sDisassembly, ", X" );
		if (line.bTargetY)
 			strcat( sDisassembly, ", Y" );
	}

	if (line.bTargetIndexed || line.bTargetIndirect)
	{
		strcat( sDisassembly, ")" );
	}

	if (line.bTargetIndirect)
	{
		if (line.bTargetY)
 			strcat( sDisassembly, ", Y" );
	}
*/
	char sTarget[ 32 ];

	if (line.bTargetValue || line.bTargetRelative || line.bTargetImmediate)
	{
		if (line.bTargetRelative)
			strcpy( sTarget, line.sTargetValue );
		else
		if (line.bTargetImmediate)
		{
			strcat( sDisassembly, "#" );
			Util_SafeStrCpy( sTarget, line.sTarget, sizeof(sTarget) );
			sTarget[sizeof(sTarget)-1] = 0;
		}
		else
			sprintf( sTarget, g_aOpmodes[ line.iOpmode ].m_sFormat, line.nTarget );

		strcat( sDisassembly, "$" );
		strcat( sDisassembly, sTarget );
	}
}

//===========================================================================
// DrawDisassemblyLine moved to Debugger_View_Code.cpp

// Optionally copy the flags to pText_
//===========================================================================
// DrawFlags moved to Debugger_View_Code.cpp

//===========================================================================
// DrawMemory moved to Debugger_View_Memory.cpp

//===========================================================================
// DrawRegister moved to Debugger_View_Memory.cpp

//===========================================================================
// DrawRegisters moved to Debugger_View_Memory.cpp


// 2.9.0.3
//===========================================================================
// _DrawSoftSwitchHighlight moved to Debugger_View_Memory.cpp

// _DrawSoftSwitchAddress moved to Debugger_View_Memory.cpp

// _DrawSoftSwitch moved to Debugger_View_Memory.cpp

// _DrawTriStateSoftSwitch moved to Debugger_View_Memory.cpp

/*
	Debugger: Support LC status and memory
	https://github.com/AppleWin/AppleWin/issues/406

	Bank2       Bank1       First Access, Second Access
	-----------------------------------------------
	C080        C088        Read RAM,     Write protect    MF_HIGHRAM   ~MF_WRITERAM
	C081        C089        Read ROM,     Write enable    ~MF_HIGHRAM    MF_WRITERAM
	C082        C08A        Read ROM,     Write protect   ~MF_HIGHRAM   ~MF_WRITERAM
	C083        C08B        Read RAM,     Write enable     MF_HIGHRAM    MF_WRITERAM
	c084        C08C        same as C080/C088
	c085        C08D        same as C081/C089
	c086        C08E        same as C082/C08A
	c087        C08F        same as C083/C08B
	MF_BANK2   ~MF_BANK2

	NOTE: Saturn 128K uses C084 .. C087 and C08C .. C08F to select Banks 0 .. 7 !
*/
// 2.9.0.4 Draw Language Card Bank Usage
// @param iBankDisplay Either 1 or 2
//===========================================================================
// _DrawSoftSwitchLanguageCardBank moved to Debugger_View_Memory.cpp

// _DrawSoftSwitchMainAuxBanks moved to Debugger_View_Memory.cpp

// DrawSoftSwitches moved to Debugger_View_Memory.cpp


//===========================================================================
// DrawSourceLine moved to Debugger_View_Code.cpp


//===========================================================================
// DrawStack moved to Debugger_View_Code.cpp

// DrawTargets moved to Debugger_View_Memory.cpp

// DrawWatches moved to Debugger_View_Memory.cpp

// DrawZeroPagePointers moved to Debugger_View_Memory.cpp


// Sub Windows ____________________________________________________________________________________

//===========================================================================
// DrawSubWindow_Console moved to Debugger_View_Console.cpp

//===========================================================================
// DrawSubWindow_Data moved to Debugger_View_Memory.cpp

// Video scanner info functions moved to Debugger_View_Memory.cpp

//===========================================================================
void DrawSubWindow_Info ( Update_t bUpdate, int iWindow )
{
  (void)iWindow;
	if (g_iWindowThis == WINDOW_CONSOLE)
		return;

	// Left Side
		int yRegs     = 0; // 12
		int yStack    = yRegs  + MAX_DISPLAY_REGS_LINES  + 0; // 0
		int yTarget   = yStack + MAX_DISPLAY_STACK_LINES - 1; // 9
		int yZeroPage = 16; // yTarget
		int ySoft = yZeroPage + (2 * MAX_DISPLAY_ZEROPAGE_LINES) + !SOFTSWITCH_LANGCARD;
		int yBeam = ySoft - 3;

		if (bUpdate & UPDATE_VIDEOSCANNER) {
			DrawVideoScannerInfo(yBeam);
    }

		if ((bUpdate & UPDATE_REGS) || (bUpdate & UPDATE_FLAGS)) {
			DrawRegisters( yRegs );
    }

		if (bUpdate & UPDATE_STACK) {
			DrawStack( yStack );
    }

		// 2.7.0.2 Fixed: Debug build of debugger force display all CPU info window wasn't calling DrawTargets()
		bool bForceDisplayTargetPtr = DEBUG_FORCE_DISPLAY || (g_bConfigInfoTargetPointer);
		if (bForceDisplayTargetPtr || (bUpdate & UPDATE_TARGETS)) {
			DrawTargets( yTarget );
    }

		if (bUpdate & UPDATE_ZERO_PAGE) {
			DrawZeroPagePointers( yZeroPage );
    }

		DrawSoftSwitches( ySoft );

	// Right Side
		int yBreakpoints = 0;
		int yWatches     = yBreakpoints + MAX_BREAKPOINTS; // MAX_DISPLAY_BREAKPOINTS_LINES; // 7
		const unsigned int numVideoScannerInfoLines = 4;		// There used to be 2 extra watches (and each watch is 2 lines)
		int yMemory      = yWatches + numVideoScannerInfoLines + (MAX_WATCHES*2); // MAX_DISPLAY_WATCHES_LINES    ; // 14 // 2.7.0.15 Fixed: Memory Dump was over-writing watches

		bool bForceDisplayBreakpoints = DEBUG_FORCE_DISPLAY || (g_nBreakpoints > 0); // 2.7.0.11 Fixed: Breakpoints and Watches no longer disappear.
		if ( bForceDisplayBreakpoints || (bUpdate & UPDATE_BREAKPOINTS)) {
			DrawBreakpoints( yBreakpoints );
    }

		bool bForceDisplayWatches = DEBUG_FORCE_DISPLAY || (g_nWatches > 0); // 2.7.0.11 Fixed: Breakpoints and Watches no longer disappear.
		if ( bForceDisplayWatches || (bUpdate & UPDATE_WATCH)) {
			DrawWatches( yWatches );
    }

		g_nDisplayMemoryLines = MAX_DISPLAY_MEMORY_LINES_1;

		bool bForceDisplayMemory1 = DEBUG_FORCE_DISPLAY || (g_aMemDump[0].bActive);
		if (bForceDisplayMemory1 || (bUpdate & UPDATE_MEM_DUMP)) {
			DrawMemory( yMemory, 0 ); // g_aMemDump[0].nAddress, g_aMemDump[0].eDevice);
    }

		yMemory += (g_nDisplayMemoryLines + 1);
		g_nDisplayMemoryLines = MAX_DISPLAY_MEMORY_LINES_2;

		bool bForceDisplayMemory2 = DEBUG_FORCE_DISPLAY || (g_aMemDump[1].bActive);
		if (bForceDisplayMemory2 || (bUpdate & UPDATE_MEM_DUMP)) {
			DrawMemory( yMemory, 1 ); // g_aMemDump[1].nAddress, g_aMemDump[1].eDevice);
    }
}

//===========================================================================
void DrawWindowBackground_Main(int iWindow);
void DrawWindowBackground_Info(int iWindow);

void InitDisasm(void)
{
  if (!g_aOpcodes)
  {
    g_aOpcodes = &g_aOpcodes65C02[0];
  }
  g_nDisasmCurAddress = regs.pc;
  DisasmCalcTopBotAddress();
}

void UpdateDisplay(Update_t bUpdate)
{
  static int spDrawMutex = false;

  if (spDrawMutex)
    return;

  spDrawMutex = true;

  // Hack: Full screen console scrolled, "erase" left over console lines
  if (g_iWindowThis == WINDOW_CONSOLE)
    bUpdate |= UPDATE_BACKGROUND;

  if (bUpdate & UPDATE_BACKGROUND)
  {
    // linux
  }

  if ((bUpdate & UPDATE_BREAKPOINTS)
//      || (bUpdate & UPDATE_DISASM)
      || (bUpdate & UPDATE_FLAGS)
      || (bUpdate & UPDATE_MEM_DUMP)
      || (bUpdate & UPDATE_REGS)
      || (bUpdate & UPDATE_STACK)
      || (bUpdate & UPDATE_SYMBOLS)
      || (bUpdate & UPDATE_TARGETS)
      || (bUpdate & UPDATE_WATCH)
      || (bUpdate & UPDATE_ZERO_PAGE))
  {
    bUpdate |= UPDATE_BACKGROUND;
    bUpdate |= UPDATE_CONSOLE_INPUT;
  }

  if (bUpdate & UPDATE_BACKGROUND)
  {
    if (g_iWindowThis != WINDOW_CONSOLE)
    {
      DrawWindowBackground_Main(g_iWindowThis);
      DrawWindowBackground_Info(g_iWindowThis);
    }
  }

  switch (g_iWindowThis)
  {
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

  StretchBltMemToFrameDC();

  spDrawMutex = false;
}

void DebugBegin ()
{
  // This is called every time the debugger is entered.
  g_state.mode = MODE_DEBUG;

  AllocateDebuggerMemDC();
  InitDisasm();

  g_state.mode = MODE_DEBUG;
  FrameRefreshStatus(DRAW_TITLE);

  UpdateDisplay( UPDATE_ALL );
}

void DebugDestroy ()
{
  DebugEnd();

  // TODO: Symbols_Clear()
  for( int iTable = 0; iTable < NUM_SYMBOL_TABLES; iTable++ )
  {
    _CmdSymbolsClear( (SymbolTable_Index_e) iTable );
  }
  // TODO: DataDisassembly_Clear()
}

void DebugEnd ()
{
  // Stepping ... calls us when key hit?!  FrameWndProc() ProcessButtonClick() DebugEnd()
  if (g_bProfiling)
  {
    // See: .csv / .txt note in CmdProfile()
    ProfileFormat( true, PROFILE_FORMAT_TAB ); // Export in Excel-ready text format.
    ProfileSave();
  }

  if (g_hTraceFile)
  {
    fclose(g_hTraceFile);
    g_hTraceFile = NULL;
  }

  extern std::vector<int> g_vMemorySearchResults;
  g_vMemorySearchResults.erase( g_vMemorySearchResults.begin(), g_vMemorySearchResults.end() );

  g_state.mode = MODE_RUNNING;

  ReleaseDebuggerMemDC();
}

void DebugInitialize()
{
  AllocateDebuggerMemDC();

  VerifyDebuggerCommandTable();

  AssemblerOff(); // update prompt

  extern conchar_t g_aConsoleDisplay[ CONSOLE_HEIGHT ][ CONSOLE_WIDTH ];
  memset( g_aConsoleDisplay, 0, sizeof( g_aConsoleDisplay ) ); // CONSOLE_WIDTH * CONSOLE_HEIGHT );
  ConsoleInputReset();

  for( int iWindow = 0; iWindow < NUM_WINDOWS; iWindow++ )
  {
    WindowSplit_t *pWindow = & g_aWindowConfig[ iWindow ];

    pWindow->bSplit = false;
    pWindow->eTop = (Window_e) iWindow;
    pWindow->eBot = (Window_e) iWindow;
  }

  g_iWindowThis = WINDOW_CODE;
  g_iWindowLast = WINDOW_CODE;

  WindowUpdateDisasmSize();

  ConfigColorsReset();

  WindowUpdateConsoleDisplayedSize();

  // CLEAR THE BREAKPOINT AND WATCH TABLES
  memset( g_aBreakpoints     , 0, MAX_BREAKPOINTS       * sizeof(Breakpoint_t));
  memset( g_aWatches         , 0, MAX_WATCHES           * sizeof(Watches_t) );
  memset( g_aZeroPagePointers, 0, MAX_ZEROPAGE_POINTERS * sizeof(ZeroPagePointers_t));

  // Load Main, Applesoft, and User Symbols
  extern bool g_bSymbolsDisplayMissingFile;
  g_bSymbolsDisplayMissingFile = false;

  g_iCommand = CMD_SYMBOLS_ROM;
  CmdSymbolsLoad(0);

  g_iCommand = CMD_SYMBOLS_APPLESOFT;
  CmdSymbolsLoad(0);

  for (int iFont = 0; iFont < NUM_FONTS; iFont++)
  {
    g_aFontConfig[ iFont ]._nFontHeight   = CONSOLE_FONT_HEIGHT;
    g_aFontConfig[ iFont ]._nFontWidthAvg = CONSOLE_FONT_WIDTH;
    g_aFontConfig[ iFont ]._nFontWidthMax = CONSOLE_FONT_WIDTH;
    g_aFontConfig[ iFont ]._nLineHeight   = CONSOLE_FONT_HEIGHT;
  }

  _UpdateWindowFontHeights( g_aFontConfig[ FONT_DISASM_DEFAULT ]._nFontHeight );

  {
    g_iConfigDisasmBranchType = DISASM_BRANCH_FANCY;
  }

  //  ConsoleInputReset(); already called in DebugInitialize()
  char sText[ CONSOLE_WIDTH ];

  // Check all summary help to see if it fits within the console
  for (int iCmd = 0; iCmd < NUM_COMMANDS; iCmd++ )
  {
    char *pHelp = g_aCommands[ iCmd ].pHelpSummary;
    if (pHelp)
    {
      int nLen = (int)strlen( pHelp ) + 2;
      if (nLen > (CONSOLE_WIDTH-1))
      {
        ConsoleBufferPushFormat( sText, "Warning: %s help is %d chars",
          pHelp, nLen );
      }
    }
  }

  CmdMOTD(0);
}

//===========================================================================
void DrawSubWindow_IO(Update_t bUpdate)
{
  (void)bUpdate;
}

//===========================================================================
void DrawSubWindow_Source2(Update_t bUpdate)
{
  (void)bUpdate;
}

void DrawWindowBottom(Update_t bUpdate, int iWindow)
{
  if (!g_aWindowConfig[iWindow].bSplit)
    return;

  WindowSplit_t* pWindow = &g_aWindowConfig[iWindow];
  g_pDisplayWindow = pWindow;

  if (pWindow->eBot == WINDOW_DATA)
    DrawWindow_Data(bUpdate);
  else if (pWindow->eBot == WINDOW_SOURCE)
    DrawSubWindow_Source2(bUpdate);
}

// DrawSubWindow_Code moved to Debugger_View_Code.cpp

void DebugReset(void)
{
  g_videoScannerDisplayInfo.Reset();
}

// DrawWindow_Code moved to Debugger_View_Code.cpp

// Full Screen console
//===========================================================================
// DrawWindow_Console moved to Debugger_View_Console.cpp

//===========================================================================
// DrawWindow_Data moved to Debugger_View_Memory.cpp

//===========================================================================
// DrawWindow_IO moved to Debugger_View_Memory.cpp

//===========================================================================
// DrawWindow_Source moved to Debugger_View_Code.cpp

//===========================================================================
// DrawWindow_Symbols moved to Debugger_View_Memory.cpp

//===========================================================================
// DrawWindow_ZeroPage moved to Debugger_View_Memory.cpp

//===========================================================================
// DrawWindowBackground_Main moved to Debugger_View_Console.cpp

//===========================================================================
// DrawWindowBackground_Info moved to Debugger_View_Console.cpp

