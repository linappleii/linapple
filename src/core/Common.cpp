#include "core/Common.h"

#include <curl/curl.h>

#include <cstdio>

#include "apple2/SerialComms.h"

static const char TITLE_APPLE_2_[] = "Apple ][ Emulator";
static const char TITLE_APPLE_2_PLUS_[] = "Apple ][+ Emulator";
static const char TITLE_APPLE_2E_[] = "Apple //e Emulator";
static const char TITLE_APPLE_2E_ENHANCED_[] = "Enhanced Apple //e Emulator";

const char* g_pAppTitle = TITLE_APPLE_2E_ENHANCED_;

char videoDriverName[100];

eApple2Type g_Apple2Type = A2TYPE_APPLE2EENHANCED;

uint64_t cumulativecycles = 0;
uint64_t cyclenum = 0;
unsigned int emulmsec = 0;
bool g_bFullSpeed = false;
bool hddenabled = false;
unsigned int clockslot;

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
unsigned int g_dwCyclesThisFrame = 0;

FILE* g_fh = nullptr;
bool g_bDisableDirectSound = false;

SuperSerialCard sg_SSC;

unsigned int g_Slot4 = CT_Mockingboard;
CURL* g_curl = NULL;

const char* GetTitleApple2() { return TITLE_APPLE_2_; }
const char* GetTitleApple2Plus() { return TITLE_APPLE_2_PLUS_; }
const char* GetTitleApple2e() { return TITLE_APPLE_2E_; }
const char* GetTitleApple2eEnhanced() { return TITLE_APPLE_2E_ENHANCED_; }
