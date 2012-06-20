#pragma once

#ifdef _WIN32
#define FILE_SEPARATOR	TEXT('\\')
#else
#define FILE_SEPARATOR	TEXT('/')
#endif

#define FTP_SEPARATOR	TEXT('/')

// let it be our second version!
#define LINAPPLE_VERSION	2

#include <curl/curl.h>

extern FILE * spMono,*spStereo;

extern char VERSIONSTRING[];	// Contructed in WinMain()

extern TCHAR     *g_pAppTitle;

extern eApple2Type	g_Apple2Type;

extern BOOL       behind;
extern DWORD      cumulativecycles;
extern DWORD      cyclenum;
extern DWORD      emulmsec;
extern bool       g_bFullSpeed;

// Win32
//extern HINSTANCE  g_hInstance;


extern AppMode_e g_nAppMode;

extern UINT g_ScreenWidth;
extern UINT g_ScreenHeight;

extern DWORD      needsprecision;
//extern TCHAR      g_sProgramDir[MAX_PATH];
extern TCHAR      g_sCurrentDir[MAX_PATH];
extern TCHAR      g_sHDDDir[MAX_PATH];
extern TCHAR      g_sSaveStateDir[MAX_PATH];
extern TCHAR      g_sParallelPrinterFile[MAX_PATH];
// FTP vars
extern TCHAR     g_sFTPLocalDir[MAX_PATH]; // FTP Local Dir, see linapple.conf for details
extern TCHAR     g_sFTPServer[MAX_PATH]; // full path to default FTP server
extern TCHAR     g_sFTPServerHDD[MAX_PATH]; // full path to default FTP server

//extern TCHAR     g_sFTPUser[256]; // user name
//extern TCHAR     g_sFTPPass[256]; // password
extern TCHAR     g_sFTPUserPass[512]; // full login line

extern CURL *	 g_curl;


extern bool       g_bResetTiming;
extern BOOL       restart;

extern DWORD      g_dwSpeed;
extern double     g_fCurrentCLK6502;

extern int        g_nCpuCyclesFeedback;
extern DWORD      g_dwCyclesThisFrame;

extern FILE*      g_fh;				// Filehandle for log file
extern bool       g_bDisableDirectSound;	// Cmd line switch: don't init DS (so no MB support)

extern UINT		g_Slot4;	// Mockingboard or Mouse in slot4

void    SetCurrentCLK6502();
