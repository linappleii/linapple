#ifndef COMMON_GLOBALS_H
#define COMMON_GLOBALS_H

extern char *g_pAppTitle;
extern char videoDriverName[100];
extern eApple2Type g_Apple2Type;
extern bool behind;
extern unsigned int cumulativecycles;
extern unsigned int cyclenum;
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
extern CSuperSerialCard sg_SSC;
extern struct MouseInterface sg_Mouse;
extern unsigned int g_Slot4;
extern CURL *g_curl;

char* GetTitleApple2();
char* GetTitleApple2Plus();
char* GetTitleApple2e();
char* GetTitleApple2eEnhanced();

#endif
