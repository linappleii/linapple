#include "Common.h"
#include "SerialComms.h"
#include <curl/curl.h>
#include <cstdio>
static char TITLE_APPLE_2_[] = TITLE_APPLE_2;
static char TITLE_APPLE_2_PLUS_[] = TITLE_APPLE_2_PLUS;
static char TITLE_APPLE_2E_[] = TITLE_APPLE_2E;
static char TITLE_APPLE_2E_ENHANCED_[] = TITLE_APPLE_2E_ENHANCED;

char *g_pAppTitle = (char*)TITLE_APPLE_2E_ENHANCED_;

char videoDriverName[100];

eApple2Type g_Apple2Type = A2TYPE_APPLE2EENHANCED;

unsigned int cumulativecycles = 0;
unsigned int cyclenum = 0;
unsigned int emulmsec = 0;
bool g_bFullSpeed = false;
bool hddenabled = false;
unsigned int clockslot;

SystemState_t g_state = {
  MODE_LOGO, false, false, SPEED_NORMAL, 560, 384, false, 0, "", "", "", "", "Printer.txt", "", "", "", "anonymous:mymail@hotmail.com", "", true, 17030
};

double g_fCurrentCLK6502 = CLOCK_6502;
int g_nCpuCyclesFeedback = 0;
unsigned int g_dwCyclesThisFrame = 0;

FILE *g_fh = NULL;
bool g_bDisableDirectSound = false;

SuperSerialCard sg_SSC;

unsigned int g_Slot4 = CT_Mockingboard;
CURL *g_curl = NULL;

char* GetTitleApple2() { return TITLE_APPLE_2_; }
char* GetTitleApple2Plus() { return TITLE_APPLE_2_PLUS_; }
char* GetTitleApple2e() { return TITLE_APPLE_2E_; }
char* GetTitleApple2eEnhanced() { return TITLE_APPLE_2E_ENHANCED_; }
