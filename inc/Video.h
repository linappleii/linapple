#pragma once

// Types
enum VIDEOTYPE {
  VT_MONO_CUSTOM,
  VT_COLOR_STANDARD,
  VT_COLOR_TEXT_OPTIMIZED,
  VT_COLOR_TVEMU,
  VT_COLOR_HALF_SHIFT_DIM,
  VT_MONO_AMBER,
  VT_MONO_GREEN,
  VT_MONO_WHITE,
  VT_NUM_MODES
};

enum VideoFlag_e {
  VF_80COL = 0x00000001,
  VF_DHIRES = 0x00000002,
  VF_HIRES = 0x00000004,
  VF_MASK2 = 0x00000008,
  VF_MIXED = 0x00000010,
  VF_PAGE2 = 0x00000020,
  VF_TEXT = 0x00000040
};

/*long*/
enum AppleFont_e {
  // 40-Column mode is 1x Zoom (default)
  // 80-Column mode is ~0.75x Zoom (7 x 16)
  // Tiny mode is 0.5 zoom (7x8) for debugger
  APPLE_FONT_WIDTH = 14, // in pixels
  APPLE_FONT_HEIGHT = 16, // in pixels

  // Each cell has a reserved aligned pixel area (grid spacing)
  APPLE_FONT_CELL_WIDTH = 16, APPLE_FONT_CELL_HEIGHT = 16,

  // The bitmap contains 3 regions
  // Each region is 256x256 pixels = 16x16 chars
  APPLE_FONT_X_REGIONSIZE = 256, // in pixelx
  APPLE_FONT_Y_REGIONSIZE = 256, // in pixels

  // Starting Y offsets (pixels) for the regions
  APPLE_FONT_Y_APPLE_2PLUS = 0, // ][+
  APPLE_FONT_Y_APPLE_80COL = 256, // //e (inc. Mouse Text)
  APPLE_FONT_Y_APPLE_40COL = 512, // ][
};

// STANDARD /*WINDOWS*/ LINUX COLORS
#define  CREAM            0xF6
#define  MEDIUM_GRAY      0xF7
#define  DARK_GRAY        0xF8
#define  RED              0xF9
#define  GREEN            0xFA
#define  YELLOW           0xFB
#define  BLUE             0xFC
#define  MAGENTA          0xFD
#define  CYAN             0xFE
#define  WHITE            0xFF

#define RGB(r, g, b)          ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))

enum Color_Palette_Index_e {
  // Really need to have Quarter Green and Quarter Blue for Hi-Res
  BLACK,
  DARK_RED,
  DARK_GREEN,       // Half Green
  DARK_YELLOW,
  DARK_BLUE,        // Half Blue
  DARK_MAGENTA,
  DARK_CYAN,
  LIGHT_GRAY,
  MONEY_GREEN,
  SKY_BLUE,

  // OUR CUSTOM COLORS
  DEEP_RED,
  LIGHT_BLUE,
  BROWN,
  ORANGE,
  PINK,
  AQUA,

  // CUSTOM HGR COLORS (don't change order) - For tv emulation g_nAppMode
  HGR_BLACK,
  HGR_WHITE,
  HGR_BLUE,
  HGR_RED,
  HGR_GREEN,
  HGR_MAGENTA,
  HGR_GREY1,
  HGR_GREY2,
  HGR_YELLOW,
  HGR_AQUA,
  HGR_PURPLE,
  HGR_PINK,

  // USER CUSTOMIZABLE COLOR
  MONOCHROME_CUSTOM,

  // Pre-set "Monochromes"
  MONOCHROME_AMBER,
  MONOCHROME_GREEN,
  MONOCHROME_WHITE,

  DARKER_YELLOW,
  DARKEST_YELLOW,
  LIGHT_SKY_BLUE,
  DARKER_SKY_BLUE,
  DEEP_SKY_BLUE,
  DARKER_CYAN,
  DARKEST_CYAN,
  HALF_ORANGE,
  DARKER_BLUE,
  DARKER_GREEN,
  DARKEST_GREEN,
  LIGHTEST_GRAY,
  NUM_COLOR_PALETTE
};

// Globals
extern INT32 g_iStatusCycle; // cycler for status panel showing

extern BOOL g_ShowLeds; // if we should show drive leds

extern BOOL graphicsmode;
extern COLORREF monochrome;
extern DWORD g_videotype;
extern DWORD g_uVideoMode;
extern DWORD g_singlethreaded;
extern pthread_mutex_t video_draw_mutex; // drawing mutex for writing to SDL surface

// Surfaces for drawing
extern SDL_Surface *g_hLogoBitmap; // our Linux logo!
extern SDL_Surface *g_hStatusSurface;  // status panel

extern SDL_Surface *g_hSourceBitmap;
extern SDL_Surface *g_hDeviceBitmap;
extern SDL_Surface *g_origscreen; // reserved for stretching
// Prototypes

void CreateColorMixMap();

BOOL VideoApparentlyDirty();

void VideoBenchmark();

void VideoCheckPage(BOOL);

void VideoChooseColor();

void VideoDestroy();

void VideoDrawLogoBitmap(/* HDC hDstDC */);

void VideoDisplayLogo();

BOOL VideoHasRefreshed();

void VideoInitialize();

void VideoRealizePalette(/*HDC*/);

void VideoSetNextScheduledUpdate();

void VideoRedrawScreen();

void VideoRefreshScreen(uint32_t uRedrawWholeScreenVideoMode =0, bool bRedrawWholeScreen=false);

void VideoPerformRefresh();

void VideoReinitialize();

void VideoResetState();

WORD VideoGetScannerAddress(bool *pbVblBar_OUT, const DWORD uExecutedCycles);

bool VideoGetVbl(DWORD uExecutedCycles);

void VideoUpdateVbl(DWORD dwCyclesThisFrame);

void VideoUpdateFlash();

bool VideoGetSW80COL(void);
bool VideoGetSWDHIRES(void);
bool VideoGetSWHIRES(void);
bool VideoGetSW80STORE(void);
bool VideoGetSWMIXED(void);
bool VideoGetSWPAGE2(void);
bool VideoGetSWTEXT(void);
bool VideoGetSWAltCharSet(void);

DWORD VideoGetSnapshot(SS_IO_Video *pSS);

DWORD VideoSetSnapshot(SS_IO_Video *pSS);

BYTE VideoCheckMode(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);

BYTE VideoCheckVbl(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);

BYTE VideoSetMode(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
