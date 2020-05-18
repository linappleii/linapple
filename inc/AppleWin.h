#pragma once

#ifdef _WIN32
#define FILE_SEPARATOR  TEXT('\\')
#else
#define FILE_SEPARATOR  TEXT('/')
#endif

#define FTP_SEPARATOR  TEXT('/')

// let it be our second version!
#define LINAPPLE_VERSION  2

#include <curl/curl.h>

extern char *g_pAppTitle;

extern eApple2Type g_Apple2Type;

extern unsigned int cumulativecycles;
extern unsigned int cyclenum;
extern unsigned int emulmsec;
extern bool g_bFullSpeed;

extern AppMode_e g_nAppMode;

extern unsigned int g_ScreenWidth;
extern unsigned int g_ScreenHeight;

extern unsigned int needsprecision;
extern char g_sProgramDir[MAX_PATH];
extern char g_sCurrentDir[MAX_PATH];
extern char g_sHDDDir[MAX_PATH];
extern char g_sSaveStateDir[MAX_PATH];
extern char g_sParallelPrinterFile[MAX_PATH];

// FTP vars
extern char g_sFTPLocalDir[MAX_PATH]; // FTP Local Dir, see linapple.conf for details
extern char g_sFTPServer[MAX_PATH]; // full path to default FTP server
extern char g_sFTPServerHDD[MAX_PATH]; // full path to default FTP server
extern char g_sFTPUserPass[512]; // full login line

extern CURL *g_curl;

extern bool g_bResetTiming;
extern bool restart;

extern unsigned int g_dwSpeed;
extern double g_fCurrentCLK6502;

extern int g_nCpuCyclesFeedback;
extern unsigned int g_dwCyclesThisFrame;

extern FILE *g_fh;        // Filehandle for log file
extern bool g_bDisableDirectSound;  // Cmd line switch: don't init DS (so no MB support)

extern unsigned int g_Slot4;  // Mockingboard or Mouse in slot4

void SetBudgetVideo(bool);

bool GetBudgetVideo();

void SetCurrentCLK6502();

void SingleStep(bool bReinit);
