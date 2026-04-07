#include <cstdint>
#pragma once

#include "Common.h"
#include "Debugger_Color.h"
#include "Debugger_Console.h"

#define USE_APPLE_FONT   1

#define DEBUG_APPLE_FONT 0

#define APPLE_FONT_NEW            1

#if APPLE_FONT_NEW
	#define APPLE_FONT_BITMAP_PADDED  0
#else
	#define APPLE_FONT_BITMAP_PADDED  1
#endif

	enum ConsoleFontSize_e
	{
		CONSOLE_FONT_GRID_X = 7,
		CONSOLE_FONT_GRID_Y = 8,

		CONSOLE_FONT_WIDTH  = 7,
		CONSOLE_FONT_HEIGHT = 8,
	};

	extern int g_hConsoleBrushFG;
	extern int g_hConsoleBrushBG;

	enum
	{
		DISPLAY_HEIGHT = 384,
		MAX_DISPLAY_LINES  = DISPLAY_HEIGHT / CONSOLE_FONT_HEIGHT,
	};

	int GetConsoleTopPixels( int y );

	extern FontConfig_t g_aFontConfig[ NUM_FONTS  ];

	void DebuggerSetColorFG( unsigned int nRGB );
	void DebuggerSetColorBG( unsigned int nRGB, bool bTransparent = false );

	void PrintGlyph      ( const int x, const int y, const int iChar );
	int  PrintText       ( const char * pText, RECT & rRect );
	int  PrintTextCursorX( const char * pText, RECT & rRect );
	int  PrintTextCursorY( const char * pText, RECT & rRect );

	void PrintTextColor  ( const conchar_t * pText, RECT & rRect );

	void GetDebugViewPortScale  (float *x, float *y);

	void DrawWindow_Source      (Update_t bUpdate);

	void DrawBreakpoints      ( int line);
	void DrawConsoleInput     ();
	void DrawConsoleLine      ( const conchar_t * pText, int y);
	void DrawConsoleCursor    ();

	int GetDisassemblyLine(  const unsigned short nOffset, DisasmLine_t & line_ );
	unsigned short DrawDisassemblyLine  ( int line, const unsigned short offset );
	void FormatDisassemblyLine( const DisasmLine_t & line, char *sDisassembly_, const int nBufferSize );
	void FormatOpcodeBytes    ( unsigned short nBaseAddress, DisasmLine_t & line_ );
	void FormatNopcodeBytes   ( unsigned short nBaseAddress, DisasmLine_t & line_ );

	void DrawFlags            ( int line, unsigned short nRegFlags, char* pFlagNames_);
	void DrawStack            ( int line);
	void DrawMemory           ( int line, int iMemDump );
	void DrawRegisters        ( int line );
	void DrawSoftSwitches     ( int iSoftSwitch );
	void DrawTargets          ( int line);
	void DrawWatches          ( int line);
	void DrawZeroPagePointers ( int line );
	void DrawVideoScannerInfo ( int line );

	extern void AllocateDebuggerMemDC(void);
	extern void ReleaseDebuggerMemDC(void);
	extern void StretchBltMemToFrameDC(void);
	bool CanDrawDebugger(void);

	void InitDisasm(void);
	void UpdateDisplay(Update_t bUpdate);

	enum DebugVirtualTextScreen_e
	{
		DEBUG_VIRTUAL_TEXT_WIDTH  = 80,
		DEBUG_VIRTUAL_TEXT_HEIGHT = 43
	};

	extern char g_aDebuggerVirtualTextScreen[ DEBUG_VIRTUAL_TEXT_HEIGHT ][ DEBUG_VIRTUAL_TEXT_WIDTH ];
	extern size_t Util_GetDebuggerText( char* &pText_ ); // Same API as Util_GetTextScreen()

	extern uint64_t g_nCumulativeCycles;
	class VideoScannerDisplayInfo
	{
	public:
		VideoScannerDisplayInfo(void) : isDecimal(false), isHorzReal(false), isAbsCycle(false),
										lastCumulativeCycles(0), cycleDelta(0) {}
		void Reset(void) { lastCumulativeCycles = g_nCumulativeCycles; cycleDelta = 0; }

		bool isDecimal;
		bool isHorzReal;
		bool isAbsCycle;

		uint64_t lastCumulativeCycles;
		unsigned int cycleDelta;
	};

	void DrawWindow_Code        (Update_t bUpdate);
	void DrawWindow_Console     (Update_t bUpdate);
	void DrawWindow_Data        (Update_t bUpdate);
	void DrawWindow_IO          (Update_t bUpdate);
	void DrawWindow_Symbols     (Update_t bUpdate);
	void DrawWindow_ZeroPage    (Update_t bUpdate);

	void DrawSourceLine(int iSourceLine, RECT & rect);

	char ColorizeSpecialChar(char * sText, unsigned char nData, const MemoryView_e iView,
		const int iAsciBackground = BG_INFO, const int iTextForeground = FG_DISASM_CHAR,
		const int iHighBackground = BG_INFO_CHAR, const int iHighForeground = FG_INFO_CHAR_HI,
		const int iCtrlBackground = BG_INFO_CHAR, const int iCtrlForeground = FG_INFO_CHAR_LO);

	void SetupColorsHiLoBits(bool bHighBit, bool bCtrlBit,
		int iTextBG, int iTextFG, int iHighBG, int iHighFG, int iCtrlBG, int iCtrlFG);

	void DrawWindowBottom(Update_t bUpdate, int iWindow);
	void DrawSubWindow_Info(Update_t bUpdate, int iWindow);
	void DrawSubWindow_Code(int iWindow);
	void DrawSubWindow_Source(Update_t bUpdate);
	void DrawSubWindow_Source2(Update_t bUpdate);
	void DrawSubWindow_IO(Update_t bUpdate);
	void FillRect(const RECT *r, int Brush);
	void DrawSubWindow_Symbols(Update_t bUpdate);
	void DrawSubWindow_ZeroPage(Update_t bUpdate);
	void DrawSubWindow_Console(Update_t bUpdate);
	void DrawWindow_Data(Update_t bUpdate);
	void DrawWindow_IO(Update_t bUpdate);
	void DrawWindow_Symbols(Update_t bUpdate);
	void DrawWindow_ZeroPage(Update_t bUpdate);
	void DrawWindow_Console(Update_t bUpdate);
	void DrawWindowBackground_Main(int iWindow);
	void DrawWindowBackground_Info(int iWindow);
	void DrawRegister(int line, const char* name, const int nBytes, const unsigned short nValue, int iSource);
	void GetTargets_IgnoreDirectJSRJMP(const unsigned char iOpcode, int& nTargetPointer);

	extern VideoScannerDisplayInfo g_videoScannerDisplayInfo;
