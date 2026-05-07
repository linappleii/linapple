#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#pragma once

// RAII wrapper for FILE*
using FilePtr = std::unique_ptr<FILE, int (*)(FILE*)>;

// Forward declarations for configuration
auto ConfigLoadInt(const char* section, const char* key, uint32_t* value)
    -> bool;
auto ConfigLoadBool(const char* section, const char* key, bool* value) -> bool;
auto ConfigLoadString(const char* section, const char* key, std::string* value)
    -> bool;
void ConfigSaveInt(const char* section, const char* key, uint32_t value);

enum eIRQSRC {
  IS_6522 = 0,
  IS_SPEECH,
  IS_SSC,
  IS_MOUSE,
  IS_SLOT1,
  IS_SLOT2,
  IS_SLOT3,
  IS_SLOT4,
  IS_SLOT5,
  IS_SLOT6,
  IS_SLOT7
};

// Configuration functions for type safety
#define USE_SPEECH_API

const double M14 = (157500000.0 / 11.0);  // 14.3181818... * 10^6
const double CLOCK_6502 =
    ((M14 * 65.0) / 912.0);  // 65 cycles per 912 14M clocks

// The effective Z-80 clock rate is 2.041MHz
const double CLK_Z80 = (CLOCK_6502 * 2);

const uint32_t uCyclesPerLine = 65;  // 25 cycles of HBL & 40 cycles of HBL
const uint32_t uVisibleLinesPerFrame = 64 * 3;  // 192
const uint32_t uLinesPerFrame =
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

constexpr int PATH_MAX_LEN = 260;

#define SCREEN_WIDTH 560
#define SCREEN_HEIGHT 384

#define VIEWPORTX 5
#define VIEWPORTY 5
#define VIEWPORTCX 560
#define VIEWPORTCY 384

using SystemState_t = struct SystemState_tag {
  AppMode_e mode;
  bool restart;
  bool fullscreen;
  uint32_t dwSpeed;
  uint32_t ScreenWidth;
  uint32_t ScreenHeight;
  bool bResetTiming;
  uint32_t needsprecision;
  char sProgramDir[PATH_MAX_LEN];
  char sCurrentDir[PATH_MAX_LEN];
  char sHDDDir[PATH_MAX_LEN];
  char sSaveStateDir[PATH_MAX_LEN];
  char sParallelPrinterFile[PATH_MAX_LEN];
  char sFTPLocalDir[PATH_MAX_LEN];
  char sFTPServer[PATH_MAX_LEN];
  char sFTPServerHDD[PATH_MAX_LEN];
  char sFTPUserPass[512];
  char sDebuggerScript[PATH_MAX_LEN];
  bool bVideoScannerNTSC;
  uint32_t dwClksPerFrame;
};

extern SystemState_t g_state;

constexpr int SPEED_MIN = 0;
constexpr int SPEED_NORMAL = 10;
constexpr int SPEED_MAX = 40;

constexpr uint32_t DRAW_BACKGROUND = 1;
constexpr uint32_t DRAW_LEDS = 2;
constexpr uint32_t DRAW_TITLE = 4;
constexpr uint32_t DRAW_BUTTON_DRIVES = 8;

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
constexpr const char* REGVALUE_COMPUTER_EMULATION = "Computer Emulation";
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
constexpr const char* REGVALUE_CLOCK_ENABLED = "Clock Enable";

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

// Native compatibility types
using ColorRef_t = uint32_t;

using Point_t = struct Point_tag {
  int32_t x;
  int32_t y;
};

using Rect_t = struct Rect_tag {
  int32_t left;
  int32_t top;
  int32_t right;
  int32_t bottom;
};

typedef struct DiskImage_tag {
  int unused;
}* DiskImagePtr_t;

static inline auto IsCharLower(char ch) -> bool {
  return (ch >= 'a' && ch <= 'z');
}

static inline auto IsCharUpper(char ch) -> bool {
  return (ch >= 'A' && ch <= 'Z');
}

#ifdef __cplusplus
extern "C" {
#endif
typedef uint8_t (*iofunction)(uint16_t nPC, uint16_t nAddr, uint8_t nWriteFlag,
                              uint8_t nWriteValue, uint32_t nCyclesLeft);
#ifdef __cplusplus
}
#endif

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

enum eApple2Language {
  A2LANG_US = 1,
  A2LANG_UK,
  A2LANG_FR,
  A2LANG_DE,
  A2LANG_JP_ROMAN,
  A2LANG_JP_KANA
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
