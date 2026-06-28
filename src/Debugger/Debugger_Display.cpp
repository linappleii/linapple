#include "core/Common.h"
#include <cassert>
#include <algorithm>
#include <cstddef>
#include <unistd.h>

#include "Debugger_Display.h"
#include "Debug.h"
#include "Debugger_Cmd_CPU.h"
#include "Debugger_Color.h"
#include "Debugger_Help.h"
#include "Debugger_Console.h"
#include "Debugger_Breakpoints.h"
#include "Debugger_Bookmarks.h"
#include "Debugger_Memory.h"
#include "Debugger_Cmd_Window.h"
#include "Debugger_DisassemblerData.h"
#include "apple2/Video.h"
#include "apple2/Memory.h"
#include "apple2/CPU.h"
#include "core/Log.h"
#include "frontends/common/VideoStretch.h"
#include "core/Asset.h"

#include "charset40.xpm" // US/default

enum {
DEBUG_FORCE_DISPLAY = 0
};

// Globals __________________________________________________________________

VideoSurface *g_hDebugScreen = nullptr;
VideoSurface *g_hDebugCharset = nullptr;

ColorRef_t g_hConsoleBrushFG = WHITE;
ColorRef_t g_hConsoleBrushBG = BLACK;

FontConfig_t g_aFontConfig[ NUM_FONTS ];
char g_aDebuggerVirtualTextScreen[DEBUG_VIRTUAL_TEXT_HEIGHT][DEBUG_VIRTUAL_TEXT_WIDTH];

extern int g_iWindowLast;
extern int g_iWindowThis;
extern WindowSplit_t g_aWindowConfig[ NUM_WINDOWS ];

extern int g_nDisasmWinHeight;
extern int g_nConsoleDisplayLines;
int g_nDisplayMemoryLines = 8;
VideoScannerDisplayInfo g_videoScannerDisplayInfo;

// Prototypes _______________________________________________________________

extern void DisasmInit();
extern auto _CmdSymbolsClear( SymbolTable_Index_e eSymbolTable ) -> Update_t;
extern void FrameRefreshStatus(int);
extern auto _6502_GetOpmodeOpbyte ( const int nBaseAddress, int & iOpmode_, int & nOpbyte_, const DisasmData_t** pData_ = nullptr ) -> int;

extern void DrawSubWindow_Symbols(Update_t bUpdate);
extern void DrawSubWindow_ZeroPage(Update_t bUpdate);
extern void DrawSubWindow_Source(Update_t bUpdate);

void DrawSubWindow_IO(Update_t) {}

const int DISPLAY_WIDTH = 560;
const int DISPLAY_DISASM_RIGHT = 353;

// Implementation ___________________________________________________________

#define  SOFTSTRECH(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, DST_W, DST_H) \
{ \
  VideoRect srcrect = {SRC_X, SRC_Y, SRC_W, SRC_H}; \
  VideoRect dstrect = {DST_X, DST_Y, DST_W, DST_H}; \
  VideoSoftStretch(SRC, &srcrect, DST, &dstrect);\
}

#define  SOFTSTRECH_MONO(SRC, SRC_X, SRC_Y, SRC_W, SRC_H, DST, DST_X, DST_Y, DST_W, DST_H) \
{ \
  VideoRect srcrect = {SRC_X, SRC_Y, SRC_W, SRC_H}; \
  VideoRect dstrect = {DST_X, DST_Y, DST_W, DST_H}; \
  VideoSoftStretchMono8(SRC, &srcrect, DST, &dstrect, hBrush, hBgBrush);\
}

//===========================================================================

void AllocateDebuggerMemDC()
{
  if (!g_hDebugScreen)
  {
    g_hDebugScreen = VideoCreateSurface(560, 384, 1);
    if (g_hDebugScreen) {
        VideoColor* pal = VideoGetOutputPalette();
        if (pal) {
            memcpy(g_hDebugScreen->palette, pal, 256 * sizeof(VideoColor));
        }
    }
    g_hDebugCharset = VideoLoadXPM(charset40_xpm);
  }
}

void ReleaseDebuggerMemDC()
{
}

void GetDebugViewPortScale(float *x, float *y)
{
  if (!g_hDebugScreen) {
    *x = 1.0f; *y = 1.0f;
    return;
  }
	float f = (static_cast<float>(g_hDebugScreen->w)) / SCREEN_WIDTH;
	*x = (f>0.01) ? f : 0.01;
	f = (static_cast<float>(g_hDebugScreen->h)) / SCREEN_HEIGHT;
	*y = (f>0.01) ? f : 0.01;
}

// Font: Apple Text
void DebuggerSetColorFG( ColorRef_t nRGB )
{
	g_hConsoleBrushFG = nRGB;
}

// Font: GDI/Console
void DebuggerSetColorBG( ColorRef_t nRGB, bool bTransparent )
{
	(void) bTransparent;
	g_hConsoleBrushBG = nRGB;
}

void FillRect(const Rect_t *r, int Brush)
{
  if (!g_hDebugScreen || !r) {
    return;
  }
  rectangle(g_hDebugScreen, r->left, r->top, r->right - r->left, r->bottom - r->top, Brush);
}

void PrintGlyph( const int x, const int y, const char glyph )
{
    if (!g_hDebugScreen || !g_hDebugCharset) return;

    char g = glyph;
    int ySrc = 64;

    if (glyph < 32)
    {
            // mouse text
            g -= 32;
            ySrc = 48;
    }
    else
    if ((glyph >= '@') && (glyph <= '_'))
    {
            g -= '@';
    }
    else
    if ((glyph >= ' ') && (glyph <= '?'))
    {
            g += 32 - ' ';
    }
    else
    if ((glyph >= '`') && (static_cast<uint8_t>(glyph) <= 127))
    {
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

        if ((col >= 0) && (col < DEBUG_VIRTUAL_TEXT_WIDTH)
        &&  (row >= 0) && (row < DEBUG_VIRTUAL_TEXT_HEIGHT)) {
            g_aDebuggerVirtualTextScreen[ row ][ col ] = glyph;
}
    }

    uint32_t hBrush = g_hConsoleBrushFG;
    uint32_t hBgBrush = g_hConsoleBrushBG;
    SOFTSTRECH_MONO(g_hDebugCharset, xSrc, ySrc, CONSOLE_FONT_WIDTH, CONSOLE_FONT_HEIGHT,
        g_hDebugScreen, x, y, CONSOLE_FONT_WIDTH, CONSOLE_FONT_HEIGHT);
}

void DebuggerPrint ( int x, int y, const char *pText )
{
    if (!pText) return;
    const int nLeft = x;
    char c = 0;
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

void DebuggerPrintColor( int x, int y, const conchar_t * pText )
{
    int nLeft = x;
    conchar_t g = 0;
    const conchar_t *pSrc = pText;

    if (!pText) {
        return;
}

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

        PrintGlyph( x, y, static_cast<char>(g & _CONSOLE_COLOR_MASK) );
        x += CONSOLE_FONT_WIDTH;
        pSrc++;
    }
}

auto CanDrawDebugger() -> bool
{
    return (g_state.mode == MODE_DEBUG) || (g_state.mode == MODE_STEPPING);
}

auto PrintText ( const char * pText, Rect_t & rRect ) -> int
{
    if (!pText) return 0;
    int nLen = strlen( pText );

#if !DEBUG_FONT_NO_BACKGROUND_TEXT
    FillRect(&rRect, g_hConsoleBrushBG );
#endif

    DebuggerPrint( rRect.left, rRect.top, pText );
    return nLen;
}

void PrintTextColor ( const conchar_t *pText, Rect_t & rRect )
{
    if (!pText) return;
#if !DEBUG_FONT_NO_BACKGROUND_TEXT
    FillRect(&rRect, g_hConsoleBrushBG );
#endif

    DebuggerPrintColor( rRect.left, rRect.top, pText );
}

auto PrintTextCursorX ( const char * pText, Rect_t & rRect ) -> int
{
    int nChars = 0;
    if (pText)
    {
        nChars = PrintText( pText, rRect );
        int nFontWidth = CONSOLE_FONT_WIDTH;
        rRect.left += (nFontWidth * nChars);
    }
    return nChars;
}

auto PrintTextCursorY ( const char * pText, Rect_t & rRect ) -> int
{
    int nChars = PrintText( pText, rRect );
    rRect.top    += CONSOLE_FONT_HEIGHT;
    rRect.bottom += CONSOLE_FONT_HEIGHT;
    return nChars;
}

// Font: GDI/Console
void ConsoleDrawChar( int x, int y, char ch )
{
	if (g_hDebugScreen == nullptr || g_hDebugCharset == nullptr) {
		return;
}

    PrintGlyph(x, y, ch);
}

// Font: GDI/Console
void ConsoleDrawText( int x, int y, const char * pText )
{
	if (g_hDebugScreen == nullptr || !pText) {
		return;
}

	const char *pSrc = pText;
	int xCur = x;
	char c = 0;

	while (pSrc && (c = *pSrc))
	{
		if (ConsoleColor_IsCharMeta( c ))
		{
			pSrc++;
			if (! *pSrc) {
				break;
}

			if (ConsoleColor_IsCharColor( *pSrc ))
			{
				DebuggerSetColorFG( g_anConsoleColor[ *pSrc - '0' ] );
			}
			else
			if (ConsoleColor_IsCharMeta( *pSrc )) // ``
			{
				ConsoleDrawChar( xCur, y, c );
				xCur += CONSOLE_FONT_GRID_X;
			}
			// else // `@  mouse text
		}
		else
		{
			ConsoleDrawChar( xCur, y, c );
			xCur += CONSOLE_FONT_GRID_X;
		}
		pSrc++;
	}
}

//===========================================================================
void DebuggerDrawChar ( int x, int y, char ch )
{
	if (g_hDebugScreen == nullptr || g_hDebugCharset == nullptr) {
		return;
}

    PrintGlyph(x, y, ch);
}

// Font: Apple Text
void DebuggerDrawText ( int x, int y, const char * pText )
{
    if (!pText) return;
	const char *pSrc = pText;
	int xCur = x;
	while (pSrc && *pSrc)
	{
		DebuggerDrawChar( xCur, y, *pSrc );
		xCur += APPLE_FONT_WIDTH;
		pSrc++;
	}
}

//===========================================================================
void DebuggerDrawCursor ( int x, int y, char ch )
{
	if (g_hDebugScreen == nullptr || g_hDebugCharset == nullptr) {
		return;
}

    PrintGlyph(x, y, ch);
}

//===========================================================================
void DrawConsoleCursor ()
{
	if (g_hDebugScreen == nullptr) {
		return;
}

	DebuggerSetColorFG( WHITE );
	DebuggerSetColorBG( BLACK, false );

	DebuggerDrawCursor(
		  g_aWindowConfig[ WINDOW_CONSOLE ].left + (g_nConsoleInputChars + g_nConsolePromptLen) * APPLE_FONT_WIDTH
		, g_aWindowConfig[ WINDOW_CONSOLE ].bottom - APPLE_FONT_HEIGHT
		, g_sConsoleCursor[0]
	);
}

//===========================================================================
void DrawConsoleInput ()
{
	if (g_hDebugScreen == nullptr) {
		return;
}

	DebuggerSetColorFG( WHITE );
	DebuggerSetColorBG( BLACK, false );

	// Draw: Prompt + Input
	DebuggerDrawText(
		  g_aWindowConfig[ WINDOW_CONSOLE ].left
		, g_aWindowConfig[ WINDOW_CONSOLE ].bottom - APPLE_FONT_HEIGHT
		, g_aConsoleInput
	);

	// Clear rest of line
	DebuggerSetColorFG( BLACK );
	VideoRect r{};
	r.x   = g_aWindowConfig[ WINDOW_CONSOLE ].left + (g_nConsoleInputChars + g_nConsolePromptLen + 1) * APPLE_FONT_WIDTH;
	r.y    = g_aWindowConfig[ WINDOW_CONSOLE ].bottom - APPLE_FONT_HEIGHT;
	r.w  = g_aWindowConfig[ WINDOW_CONSOLE ].right - r.x;
	r.h = APPLE_FONT_HEIGHT;

  rectangle(g_hDebugScreen, r.x, r.y, r.w, r.h, BLACK);
}

//===========================================================================
void DrawConsoleLine ( const conchar_t * pText, int y_coord )
{
	if (g_hDebugScreen == nullptr) {
		return;
}

	int x = g_aWindowConfig[ WINDOW_CONSOLE ].left;
	int y = g_aWindowConfig[ WINDOW_CONSOLE ].top + y_coord * APPLE_FONT_HEIGHT;

	const conchar_t *pSrc = pText;
	conchar_t g = 0;

    if (!pText) {
        // Clear line
        rectangle(g_hDebugScreen, x, y, g_aWindowConfig[WINDOW_CONSOLE].right - x, APPLE_FONT_HEIGHT, BLACK);
        return;
    }

	while (pSrc && (g = *pSrc))
	{
		DebuggerSetColorFG( ConsoleColor_GetColor( g ) );
		DebuggerDrawChar( x, y, ConsoleChar_GetChar( g ) );
		x += APPLE_FONT_WIDTH;
		pSrc++;
	}
}

auto GetConsoleTopPixels( int y ) -> int
{
	return g_aWindowConfig[ WINDOW_CONSOLE ].top + (y * CONSOLE_FONT_HEIGHT);
}

void ColorizeFlags( bool bSet, int bg_default, int fg_default)
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

void DrawSubWindow_Info(Update_t bUpdate, int iWindow)
{
	if (g_iWindowThis == WINDOW_CONSOLE) {
		return;
}

    (void)bUpdate; (void)iWindow;
	DrawRegisters( 0 );
	DrawStack( 10 );
	DrawMemory( 20, 0 );
	DrawMemory( 30, 1 );
    DrawSoftSwitches( 16 );
}

auto ColorizeSpecialChar(char * sText, uint8_t nData, const MemoryView_e iView,
    const int iAsciBackground, const int iTextForeground,
    const int iHighBackground, const int iHighForeground,
    const int iCtrlBackground, const int iCtrlForeground) -> char
{
    (void)iView; (void)iAsciBackground; (void)iTextForeground; (void)iHighBackground; (void)iHighForeground; (void)iCtrlBackground; (void)iCtrlForeground;
    bool bCtrlBit = false;

    uint8_t nByte = (nData & 0x7F);
    if (nByte < 0x20) bCtrlBit = true;

    char nChar = static_cast<char>(nByte);
    if (bCtrlBit) nChar += '@';

    if (sText) sprintf(sText, "%c", nChar);
    return nChar;
}

auto FormatCharTxtHigh(const uint8_t b, bool *pWasHi_) -> char {
    if (pWasHi_) *pWasHi_ = (b > 0x7F);
    return b & 0x7F;
}

auto FormatCharTxtCtrl(const uint8_t b, bool *pWasCtrl_) -> char {
    if (pWasCtrl_) *pWasCtrl_ = (b < 0x20);
    return (b < 0x20) ? b + '@' : b;
}

auto FormatChar4Font(const uint8_t b, bool *pWasHi_, bool *pWasLo_) -> char {
    uint8_t b1 = FormatCharTxtHigh(b, pWasHi_);
    return FormatCharTxtCtrl(b1, pWasLo_);
}

auto GetDisassemblyLine ( uint16_t nBaseAddress, DisasmLine_t & line_ ) -> int
{
  line_.Clear();

  int iOpcode = 0;
  int iOpmode = 0;
  int nOpbyte = 0;

  iOpcode = _6502_GetOpmodeOpbyte( nBaseAddress, iOpmode, nOpbyte, &line_.pDisasmData );

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

  if ((iOpmode == AM_AX) || (iOpmode == AM_ZX) || (iOpmode == AM_IZX) || (iOpmode == AM_IAX)) {
          line_.bTargetX = true;
}

  if ((iOpmode == AM_AY) || (iOpmode == AM_ZY) || (iOpmode == AM_NZY)) {
          line_.bTargetY = true;
}

  return 0; // Simplified for now
}

void GetTargets_IgnoreDirectJSRJMP(const uint8_t iOpcode, int& nTargetPointer)
{
    (void)iOpcode; (void)nTargetPointer;
}

auto FormatAddress(uint16_t nAddress, int nBytes) -> const char*
{
    static char sAddress[16];
    if (nBytes == 1) { sprintf(sAddress, "%02X", nAddress);
    } else { sprintf(sAddress, "%04X", nAddress);
}
    return sAddress;
}

void InitDisasm()
{
  for (auto & i : g_aWindowConfig) {
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
}

void DrawWindowBottom(Update_t bUpdate, int iWindow)
{
    (void)bUpdate; (void)iWindow;
}

//===========================================================================
void UpdateDisplay ( Update_t bUpdate )
{
	static int spDrawMutex = false;

	if (spDrawMutex) {
		return;
}

	spDrawMutex = true;

  AllocateDebuggerMemDC();
  if (!g_hDebugScreen) {
      spDrawMutex = false;
      return;
  }

  if (bUpdate & UPDATE_ALL)
  {
    memset(g_hDebugScreen->pixels, 0, static_cast<size_t>(g_hDebugScreen->pitch * g_hDebugScreen->h));
  }

	switch (g_iWindowThis)
	{
		case WINDOW_CODE:
			DrawWindow_Code( bUpdate );
			break;

		case WINDOW_CONSOLE:
			DrawWindow_Console( bUpdate );
			break;

		case WINDOW_DATA:
			DrawWindow_Data( bUpdate );
			break;

		case WINDOW_IO:
			DrawWindow_IO( bUpdate );
			break;

		case WINDOW_SOURCE:
			DrawWindow_Source( bUpdate );
			break;

		case WINDOW_SYMBOLS:
			DrawWindow_Symbols( bUpdate );
			break;

		case WINDOW_ZEROPAGE:
			DrawWindow_ZeroPage( bUpdate );
			break;

    default:
      break;
	}

	if ((bUpdate & UPDATE_CONSOLE_DISPLAY) || (bUpdate & UPDATE_CONSOLE_INPUT))
	{
		DrawSubWindow_Console( bUpdate );
	}

  StretchBltMemToFrameDC();

	spDrawMutex = false;
}

void DebugBegin ()
{
  // This is called every time the debugger is entered.
  g_state.mode = MODE_DEBUG;

  DebugInitialize();
  AllocateDebuggerMemDC();

  g_state.mode = MODE_DEBUG;
  FrameRefreshStatus(DRAW_TITLE);

  UpdateDisplay( UPDATE_ALL );
}

void DebugDestroy ()
{
  DebugEnd();

  for( int iTable = 0; iTable < NUM_SYMBOL_TABLES; iTable++ )
  {
    _CmdSymbolsClear( static_cast<SymbolTable_Index_e>(iTable) );
  }
}

void DebugEnd ()
{
  if (g_bProfiling)
  {
    ProfileFormat( true, PROFILE_FORMAT_TAB );
    ProfileSave();
  }

  if (g_hTraceFile)
  {
    fclose(g_hTraceFile);
    g_hTraceFile = nullptr;
  }

  extern std::vector<int> g_vMemorySearchResults;
  g_vMemorySearchResults.erase( g_vMemorySearchResults.begin(), g_vMemorySearchResults.end() );

  g_state.mode = MODE_RUNNING;

  ReleaseDebuggerMemDC();
}
