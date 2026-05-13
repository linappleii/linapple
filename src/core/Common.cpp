#include "core/Common.h"

#include <curl/curl.h>

#include <cstdio>

#include "apple2/peripherals/super_serial_card/SerialComms.h"

static const char TITLE_APPLE_2_[] = "Apple ][ Emulator";
static const char TITLE_APPLE_2_PLUS_[] = "Apple ][+ Emulator";
static const char TITLE_APPLE_2E_[] = "Apple //e Emulator";
static const char TITLE_APPLE_2E_ENHANCED_[] = "Enhanced Apple //e Emulator";

const char* g_pAppTitle = TITLE_APPLE_2E_ENHANCED_;

char videoDriverName[100];

eApple2Type g_Apple2Type = A2TYPE_APPLE2EENHANCED;
eApple2Language g_Language = A2LANG_US;
bool language_rocker_switch = false;

uint64_t cumulativecycles = 0;
uint64_t cyclenum = 0;
uint32_t emulmsec = 0;
bool g_bFullSpeed = false;
bool hddenabled = false;

SystemState_t g_state = {MODE_LOGO,
                         false,
                         false,
                         SPEED_NORMAL,
                         560,
                         384,
                         false,
                         0,
                         "",
                         "",
                         "",
                         "",
                         "Printer.txt",
                         "",
                         "",
                         "",
                         "anonymous:mymail@hotmail.com",
                         "",
                         true,
                         17030};

double g_fCurrentCLK6502 = CLOCK_6502;
int g_nCpuCyclesFeedback = 0;
uint32_t g_dwCyclesThisFrame = 0;

bool g_bDisableDirectSound = false;

SuperSerialCard sg_SSC;

uint32_t g_Slot4 = CT_Mockingboard;
CURL* g_curl = nullptr;

auto GetTitleApple2() -> const char* { return TITLE_APPLE_2_; }
auto GetTitleApple2Plus() -> const char* { return TITLE_APPLE_2_PLUS_; }
auto GetTitleApple2e() -> const char* { return TITLE_APPLE_2E_; }
auto GetTitleApple2eEnhanced() -> const char* {
  return TITLE_APPLE_2E_ENHANCED_;
}
