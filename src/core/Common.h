#include <cstdint>
#include <string>
#include <algorithm>

#pragma once

// Forward declarations for configuration
auto ConfigLoadInt(const char* section, const char* key, uint32_t* value) -> bool;
auto ConfigLoadBool(const char* section, const char* key, bool* value) -> bool;
auto ConfigLoadString(const char* section, const char* key, std::string* value) -> bool;
void ConfigSaveInt(const char* section, const char* key, uint32_t value);

enum eIRQSRC { IS_6522 = 0, IS_SPEECH, IS_SSC, IS_MOUSE };

// Configuration functions for type safety
#define USE_SPEECH_API

const double M14 = (157500000.0 / 11.0);  // 14.3181818... * 10^6
const double CLOCK_6502 =
    ((M14 * 65.0) / 912.0);  // 65 cycles per 912 14M clocks

// The effective Z-80 clock rate is 2.041MHz
const double CLK_Z80 = (CLOCK_6502 * 2);

const unsigned int uCyclesPerLine = 65;  // 25 cycles of HBL & 40 cycles of HBL
const unsigned int uVisibleLinesPerFrame = 64 * 3;  // 192
const unsigned int uLinesPerFrame =
    262;  // 64 in each third of the screen & 70 in VBL

constexpr int NUM_SLOTS = 8;

constexpr uint32_t _6502_MEM_END = 0xFFFF;
constexpr uint32_t _6502_MEM_LEN = _6502_MEM_END + 1;

constexpr char FILE_SEPARATOR = '/';
constexpr char FTP_SEPARATOR = '/';

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define RAMWORKS      // 8MB RamWorks III support
#define MOCKINGBOARD  // Mockingboard support

// Use a base freq so that sound h/w doesn't have to up/down-sample. Assume base
// freqs are 44.1KHz & 48KHz.
constexpr uint32_t SPKR_SAMPLE_RATE = 44100;  // that is for Apple][ speakers
constexpr uint32_t SAMPLE_RATE = 44100;  // that is for Phasor/Mockingboard?

enum AppMode_e {
  MODE_LOGO = 0,
  MODE_PAUSED,
  MODE_RUNNING,  // 6502 is running at normal speed (Debugger breakpoints may or
                 // may not be active)
  MODE_DEBUG,    // 6502 is paused
  MODE_STEPPING,  // 6502 is running at full speed (Debugger breakpoints always
                  // active)
  MODE_DISK_CHOOSE,  // Selecting a disk image
  MODE_EXIT,         // Application is exiting
};

constexpr int MAX_PATH = 260;

typedef struct SystemState_tag {
  AppMode_e mode;
  bool restart;
  bool fullscreen;
  unsigned int dwSpeed;
  unsigned int ScreenWidth;
  unsigned int ScreenHeight;
  bool bResetTiming;
  unsigned int needsprecision;
  char sProgramDir[MAX_PATH];
  char sCurrentDir[MAX_PATH];
  char sHDDDir[MAX_PATH];
  char sSaveStateDir[MAX_PATH];
  char sParallelPrinterFile[MAX_PATH];
  char sFTPLocalDir[MAX_PATH];
  char sFTPServer[MAX_PATH];
  char sFTPServerHDD[MAX_PATH];
  char sFTPUserPass[512];
  char sDebuggerScript[MAX_PATH];
  bool bVideoScannerNTSC;
  unsigned int dwClksPerFrame;
} SystemState_t;

extern SystemState_t g_state;

constexpr int SPEED_MIN = 0;
constexpr int SPEED_NORMAL = 10;
constexpr int SPEED_MAX = 40;

constexpr uint32_t DRAW_BACKGROUND = 1;
constexpr uint32_t DRAW_LEDS = 2;
constexpr uint32_t DRAW_TITLE = 4;
constexpr uint32_t DRAW_BUTTON_DRIVES = 8;

// Function Keys F1 - F12
constexpr int BTN_HELP = 0;
constexpr int BTN_RUN = 1;
constexpr int BTN_DRIVE1 = 2;
constexpr int BTN_DRIVE2 = 3;
constexpr int BTN_DRIVESWAP = 4;
constexpr int BTN_FULLSCR = 5;
constexpr int BTN_DEBUG = 6;
constexpr int BTN_SETUP = 7;
constexpr int BTN_CYCLE = 8;
constexpr int BTN_QUIT = 11;
// BTN_SAVEST and BTN_LOADST
constexpr int BTN_SAVEST = 10;
constexpr int BTN_LOADST = 9;

constexpr const char* TITLE_APPLE_2 = "Apple ][ Emulator";
constexpr const char* TITLE_APPLE_2_PLUS = "Apple ][+ Emulator";
constexpr const char* TITLE_APPLE_2E = "Apple //e Emulator";
constexpr const char* TITLE_APPLE_2E_ENHANCED = "Enhanced Apple //e Emulator";

constexpr const char* TITLE_PAUSED = " Paused ";
constexpr const char* TITLE_STEPPING = "Stepping";

// Configuration functions for type safety
inline auto LOAD(const char* key, uint32_t* value) -> bool {
  return ConfigLoadInt("Configuration", key, value);
}

inline auto LOAD(const char* key, bool* value) -> bool {
  return ConfigLoadBool("Configuration", key, value);
}

inline auto LOAD(const char* key, std::string* value) -> bool {
  return ConfigLoadString("Configuration", key, value);
}

inline void SAVE(const char* key, uint32_t value) {
  ConfigSaveInt("Configuration", key, value);
}

// Configuration
constexpr const char* REGVALUE_APPLE2_TYPE = "Apple2 Type";
constexpr const char* REGVALUE_SPKR_VOLUME = "Speaker Volume";
constexpr const char* REGVALUE_MB_VOLUME = "Mockingboard Volume";
constexpr const char* REGVALUE_SOUNDCARD_TYPE = "Soundcard Type";
constexpr const char* REGVALUE_KEYB_TYPE = "Keyboard Type";
constexpr const char* REGVALUE_KEYB_CHARSET_SWITCH = "Keyboard Rocker Switch";
constexpr const char* REGVALUE_SAVESTATE_FILENAME = "Save State Filename";
constexpr const char* REGVALUE_SAVE_STATE_ON_EXIT = "Save State On Exit";
constexpr const char* REGVALUE_HDD_ENABLED = "Harddisk Enable";
constexpr const char* REGVALUE_HDD_IMAGE1 = "Harddisk Image 1";
constexpr const char* REGVALUE_HDD_IMAGE2 = "Harddisk Image 2";
constexpr const char* REGVALUE_DISK_IMAGE1 = "Disk Image 1";
constexpr const char* REGVALUE_DISK_IMAGE2 = "Disk Image 2";
constexpr const char* REGVALUE_CLOCK_SLOT = "Clock Enable";

constexpr const char* REGVALUE_PPRINTER_FILENAME = "Parallel Printer Filename";
constexpr const char* REGVALUE_PRINTER_APPEND = "Append to printer file";
constexpr const char* REGVALUE_PRINTER_IDLE_LIMIT = "Printer idle limit";

constexpr const char* REGVALUE_PDL_XTRIM = "PDL X-Trim";
constexpr const char* REGVALUE_PDL_YTRIM = "PDL Y-Trim";
constexpr const char* REGVALUE_SCROLLLOCK_TOGGLE = "ScrollLock Toggle";
constexpr const char* REGVALUE_MOUSE_IN_SLOT4 = "Mouse in slot 4";

// Preferences
constexpr const char* REGVALUE_PREF_START_DIR = "Slot 6 Directory";
constexpr const char* REGVALUE_PREF_HDD_START_DIR = "HDV Starting Directory";
constexpr const char* REGVALUE_PREF_SAVESTATE_DIR = "Save State Directory";

constexpr const char* REGVALUE_SHOW_LEDS = "Show Leds";

// For FTP access
constexpr const char* REGVALUE_FTP_DIR = "FTP Server";
constexpr const char* REGVALUE_FTP_HDD_DIR = "FTP ServerHDD";

constexpr const char* REGVALUE_FTP_LOCAL_DIR = "FTP Local Dir";
constexpr const char* REGVALUE_FTP_USERPASS = "FTP UserPass";

// Win32 compatibility types
typedef void* HANDLE;
typedef uint32_t COLORREF;

typedef struct tagPOINT {
  int32_t x;
  int32_t y;
} POINT;

typedef struct tagRECT {
  int32_t left;
  int32_t top;
  int32_t right;
  int32_t bottom;
} RECT;

static inline bool IsCharLower(char ch) { return (ch >= 'a' && ch <= 'z'); }

static inline bool IsCharUpper(char ch) { return (ch >= 'A' && ch <= 'Z'); }

enum eSOUNDCARDTYPE {
  SC_UNINIT = 0,
  SC_NONE,
  SC_MOCKINGBOARD,
  SC_PHASOR
};  // Apple soundcard type

typedef unsigned char (*iofunction)(unsigned short nPC, unsigned short nAddr,
                                    unsigned char nWriteFlag,
                                    unsigned char nWriteValue,
                                    uint32_t nCyclesLeft);

typedef struct _IMAGE__ {
  int unused;
}* HIMAGE;

constexpr uint8_t APPLE2E_MASK = 0x10;
constexpr uint8_t APPLE2C_MASK = 0x20;

// NB. These get persisted to the Registry, so don't change the values for these
// enums!
enum eApple2Type {
  A2TYPE_APPLE2 = 0,
  A2TYPE_APPLE2PLUS,
  A2TYPE_APPLE2E = APPLE2E_MASK,
  A2TYPE_APPLE2EENHANCED,
  A2TYPE_MAX
};

extern eApple2Type g_Apple2Type;

inline auto IS_APPLE2() -> bool {
  return (g_Apple2Type & (APPLE2E_MASK | APPLE2C_MASK)) == 0;
}
inline auto IS_APPLE2E() -> bool { return (g_Apple2Type & APPLE2E_MASK) != 0; }
inline auto IS_APPLE2C() -> bool { return (g_Apple2Type & APPLE2C_MASK) != 0; }

enum eBUTTON { BUTTON0 = 0, BUTTON1 };
enum eBUTTONSTATE { BUTTON_UP = 0, BUTTON_DOWN };

// sizes of status panel
constexpr int STATUS_PANEL_W = 100;
constexpr int STATUS_PANEL_H = 48;
