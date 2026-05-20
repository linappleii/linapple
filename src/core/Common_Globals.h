#ifndef COMMON_GLOBALS_H
#define COMMON_GLOBALS_H

#include <curl/curl.h>

#include "core/Common.h"

extern const char* g_pAppTitle;
extern char videoDriverName[100];
extern eApple2Type g_Apple2Type;
extern eApple2Language g_Language;
extern uint64_t cumulativecycles;
extern uint64_t cyclenum;
extern uint32_t emulmsec;
extern bool g_bFullSpeed;
extern bool hddenabled;
extern SystemState_t g_state;
extern double g_fCurrentCLK6502;
extern int g_nCpuCyclesFeedback;
extern uint32_t g_dwCyclesThisFrame;
extern bool g_bDisableDirectSound;
extern uint32_t g_Slot4;
extern CURL* g_curl;

auto GetTitleApple2() -> const char*;
auto GetTitleApple2Plus() -> const char*;
auto GetTitleApple2e() -> const char*;
auto GetTitleApple2eEnhanced() -> const char*;

#endif
