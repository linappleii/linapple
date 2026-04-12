#ifndef COMMON_GLOBALS_H
#define COMMON_GLOBALS_H

#include "core/Common.h"
#include <curl/curl.h>

extern const char *g_pAppTitle;
extern char videoDriverName[100];
extern eApple2Type g_Apple2Type;
extern uint64_t cumulativecycles;
extern uint64_t cyclenum;
extern unsigned int emulmsec;
extern bool g_bFullSpeed;
extern bool hddenabled;
extern unsigned int clockslot;
extern SystemState_t g_state;
extern double g_fCurrentCLK6502;
extern int g_nCpuCyclesFeedback;
extern unsigned int g_dwCyclesThisFrame;
extern FILE *g_fh;
extern bool g_bDisableDirectSound;
extern struct SuperSerialCard sg_SSC;
extern struct MouseInterface sg_Mouse;
extern unsigned int g_Slot4;
extern CURL *g_curl;

const char* GetTitleApple2();
const char* GetTitleApple2Plus();
const char* GetTitleApple2e();
const char* GetTitleApple2eEnhanced();

#endif
