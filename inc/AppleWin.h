#include <cstdint>
#pragma once

#define FILE_SEPARATOR  '/'

#define FTP_SEPARATOR  '/'

#define LINAPPLE_VERSION  2

#include <curl/curl.h>
#include <SDL3/SDL.h>

extern char *g_pAppTitle;

extern eApple2Type g_Apple2Type;

extern unsigned int cumulativecycles;
extern unsigned int cyclenum;
extern unsigned int emulmsec;
extern bool g_bFullSpeed;

extern CURL *g_curl;

extern double g_fCurrentCLK6502;

extern int g_nCpuCyclesFeedback;
extern unsigned int g_dwCyclesThisFrame;

extern FILE *g_fh;        // Filehandle for log file
extern bool g_bDisableDirectSound;  // Cmd line switch: disable sound (so no MB support)

extern unsigned int g_Slot4;  // Mockingboard or Mouse in slot4

void SetBudgetVideo(bool);

bool GetBudgetVideo();

void SetCurrentCLK6502();

void SingleStep(bool bReinit);

uint8_t Frontend_TranslateKey(SDL_Keycode key, SDL_Keymod mod);
